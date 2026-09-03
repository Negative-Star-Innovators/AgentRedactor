"""Rewrite model_quantized.onnx so banded attention is computed structurally.

The shipped openai/privacy-filter ONNX computes attention densely:
an N x N MatMul + Softmax per layer with the +-128-token band applied only
as an additive mask (Range/Tile/LessOrEqual/And/Where). Memory scales
quadratically (~220 bytes/token^2 at fp32) and CPU inference OOMs on large
requests.

ORT's SparseAttention contrib op is not usable here: its schema
(onnxruntime v1.24.4, core/graph/contrib_ops/bert_defs.cc) states
"Only supports unidirectional attention with cache of past key and value" —
this model needs a *bidirectional* +-128 band and per-head attention sinks,
neither of which the op supports. So this script takes the documented
fallback: exact windowed-block decomposition with pure standard ONNX ops.

Per layer, the dense attention core (repeat_kv expand, N x N scores MatMul,
mask add, sink concat, softmax, AV MatMul) is replaced with:

  * Pad Q along the sequence to Np = ceil(N/S)*S blocks of S=256 queries.
  * Pad K/V by 128 on the left and (padR+128) on the right, so padded
    position p corresponds to token p-128.
  * For query block b (tokens [bS, bS+S)), gather the fixed key window
    [bS, bS+S+256) in padded coordinates — exactly the tokens that can be
    within the +-128 band of any query in the block.
  * scores = Q5 @ K5^T with GQA folded into the query rows
    ([B,NB,2,7S,64] @ [B,NB,2,64,W] -> [B,NB,2,7S,W], W=S+256).
  * Additive band mask: within the window, query row r (local pos s=r%S)
    may attend window column w iff s <= w <= s+256 (i.e. |i-j| <= 128).
  * Additive key-padding mask from the (zero-padded) attention_mask,
    gathered with the same indices — also masks the synthetic pad region.
  * Concat the per-head sink logit, softmax, slice the sink column off,
    multiply by the gathered V windows, unfold and slice back to [B,N,896].

All weights, quantized tensors and the external-data layout are untouched:
the output .onnx keeps the same external references, so the existing
model_quantized.onnx_data file (and kWeightsExpectedBytes) stay valid.

Usage:
  python make_sparse_model.py [--src PATH] [--dst PATH]

The destination .onnx expects the original model_quantized.onnx_data next to
it (copy or hardlink the file; it is byte-identical to the shipped one).
"""

import argparse
import os

import onnx
from onnx import TensorProto, helper, numpy_helper

BLOCK = 256          # S: queries per block
HALO = 128           # band half-width (|i-j| <= 128)
N_QHEADS = 14
N_KVHEADS = 2
Q_MULT = N_QHEADS // N_KVHEADS   # 7
HEAD_DIM = 64
HIDDEN = 640
ATTN_OUT = N_QHEADS * HEAD_DIM   # 896
WIN = BLOCK + 2 * HALO           # W = 512 keys per block
ROWS = N_KVHEADS * Q_MULT * BLOCK // N_KVHEADS  # 7S = 1792 rows in folded scores

NEG_INF = -3.4028234663852886e38  # same constant the shipped mask uses


def _const_i64(name, values):
    t = numpy_helper.from_array(
        __import__("numpy").array(values, dtype="int64"), name=name
    )
    return t


def _build_shared_nodes(c):
    """Nodes computed once and shared by all layers.

    c: dict of constant names. Returns dict of shared tensor names.
    """
    n = []
    A = "/model/sparse_attn/shared"

    # Sequence length N (scalar int64) — reuse the existing gather.
    seq_n = "/model/shared_dims/root_input/Gather_1/output_0"
    batch = "/model/shared_dims/root_input/Gather_0/output_0"

    n.append(helper.make_node("Add", [seq_n, c["Sm1"]], [f"{A}/n_plus"], name=f"{A}/n_plus"))
    n.append(helper.make_node("Div", [f"{A}/n_plus", c["S"]], [f"{A}/num_blocks"], name=f"{A}/num_blocks"))
    n.append(helper.make_node("Mul", [f"{A}/num_blocks", c["S"]], [f"{A}/n_padded"], name=f"{A}/n_padded"))
    n.append(helper.make_node("Sub", [f"{A}/n_padded", seq_n], [f"{A}/pad_right"], name=f"{A}/pad_right"))
    n.append(helper.make_node("Add", [f"{A}/pad_right", c["halo"]], [f"{A}/pad_right_kv"], name=f"{A}/pad_right_kv"))

    # Pad tensors (Pad opset>=11 takes a [2*rank] pads tensor).
    n.append(helper.make_node("Unsqueeze", [f"{A}/pad_right", c["ax0"]], [f"{A}/pad_right_1d"], name=f"{A}/pad_right_1d"))
    n.append(helper.make_node("Unsqueeze", [f"{A}/pad_right_kv", c["ax0"]], [f"{A}/pad_right_kv_1d"], name=f"{A}/pad_right_kv_1d"))
    n.append(helper.make_node("Concat",
                              [c["zeros3"], c["zero1"], f"{A}/pad_right_1d", c["zero1"]],
                              [f"{A}/pads_q"], name=f"{A}/pads_q", axis=0))
    n.append(helper.make_node("Concat",
                              [c["zero1"], c["halo1"], c["zero1"], c["zero1"], f"{A}/pad_right_kv_1d", c["zero1"]],
                              [f"{A}/pads_kv"], name=f"{A}/pads_kv", axis=0))
    n.append(helper.make_node("Concat",
                              [c["zero1"], c["halo1"], c["zero1"], f"{A}/pad_right_kv_1d"],
                              [f"{A}/pads_mask"], name=f"{A}/pads_mask", axis=0))

    # Gather indices IDX[NB, W] = b*S + w (padded K/V coordinates).
    n.append(helper.make_node("Range", [c["zero"], f"{A}/num_blocks", c["one"]], [f"{A}/block_ids"], name=f"{A}/block_ids"))
    n.append(helper.make_node("Mul", [f"{A}/block_ids", c["S"]], [f"{A}/block_starts"], name=f"{A}/block_starts"))
    n.append(helper.make_node("Range", [c["zero"], c["W"], c["one"]], [f"{A}/win_ids"], name=f"{A}/win_ids"))
    n.append(helper.make_node("Unsqueeze", [f"{A}/block_starts", c["ax1"]], [f"{A}/block_starts_2d"], name=f"{A}/block_starts_2d"))
    n.append(helper.make_node("Unsqueeze", [f"{A}/win_ids", c["ax0"]], [f"{A}/win_ids_2d"], name=f"{A}/win_ids_2d"))
    n.append(helper.make_node("Add", [f"{A}/block_starts_2d", f"{A}/win_ids_2d"], [f"{A}/gather_idx"], name=f"{A}/gather_idx"))

    # Key-padding mask windows from the (zero-padded) attention_mask.
    n.append(helper.make_node("Pad", ["attention_mask", f"{A}/pads_mask"], [f"{A}/mask_pad"], name=f"{A}/mask_pad"))
    n.append(helper.make_node("Gather", [f"{A}/mask_pad", f"{A}/gather_idx"], [f"{A}/mask_win"], name=f"{A}/mask_win", axis=1))
    n.append(helper.make_node("Equal", [f"{A}/mask_win", c["zero"]], [f"{A}/mask_is_pad"], name=f"{A}/mask_is_pad"))
    n.append(helper.make_node("Where", [f"{A}/mask_is_pad", c["neg"], c["zerof"]], [f"{A}/mask_add"], name=f"{A}/mask_add"))
    n.append(helper.make_node("Unsqueeze", [f"{A}/mask_add", c["ax23"]], [f"{A}/mask_add5"], name=f"{A}/mask_add5"))

    # Static band mask [7S, W]: row r (local s = r % S) attends cols w in [s, s+S].
    n.append(helper.make_node("Range", [c["zero"], c["7S"], c["one"]], [f"{A}/rows"], name=f"{A}/rows"))
    n.append(helper.make_node("Mod", [f"{A}/rows", c["S"]], [f"{A}/row_local"], name=f"{A}/row_local"))
    n.append(helper.make_node("Unsqueeze", [f"{A}/row_local", c["ax1"]], [f"{A}/row_local_2d"], name=f"{A}/row_local_2d"))
    n.append(helper.make_node("Add", [f"{A}/row_local_2d", c["S"]], [f"{A}/row_hi"], name=f"{A}/row_hi"))
    n.append(helper.make_node("GreaterOrEqual", [f"{A}/win_ids_2d", f"{A}/row_local_2d"], [f"{A}/band_ge"], name=f"{A}/band_ge"))
    n.append(helper.make_node("LessOrEqual", [f"{A}/win_ids_2d", f"{A}/row_hi"], [f"{A}/band_le"], name=f"{A}/band_le"))
    n.append(helper.make_node("And", [f"{A}/band_ge", f"{A}/band_le"], [f"{A}/band_ok"], name=f"{A}/band_ok"))
    n.append(helper.make_node("Where", [f"{A}/band_ok", c["zerof"], c["neg"]], [f"{A}/band_add"], name=f"{A}/band_add"))
    n.append(helper.make_node("Reshape", [f"{A}/band_add", c["band5"]], [f"{A}/band_add5"], name=f"{A}/band_add5"))

    # Dynamic sink expand shape [B, NB, 2, 7S, 1].
    n.append(helper.make_node("Unsqueeze", [batch, c["ax0"]], [f"{A}/batch_1d"], name=f"{A}/batch_1d"))
    n.append(helper.make_node("Unsqueeze", [f"{A}/num_blocks", c["ax0"]], [f"{A}/nb_1d"], name=f"{A}/nb_1d"))
    n.append(helper.make_node("Concat",
                              [f"{A}/batch_1d", f"{A}/nb_1d", c["kv1"], c["7S1"], c["one1"]],
                              [f"{A}/sinks_shape"], name=f"{A}/sinks_shape", axis=0))

    # Dynamic final-slice end [N].
    n.append(helper.make_node("Unsqueeze", [seq_n, c["ax0"]], [f"{A}/seq_n_1d"], name=f"{A}/seq_n_1d"))

    return {
        "pads_q": f"{A}/pads_q",
        "pads_kv": f"{A}/pads_kv",
        "gather_idx": f"{A}/gather_idx",
        "mask_add5": f"{A}/mask_add5",
        "band_add5": f"{A}/band_add5",
        "sinks_shape": f"{A}/sinks_shape",
        "seq_n_1d": f"{A}/seq_n_1d",
    }, n


def _build_layer_nodes(layer, sh, c):
    """Exact block-windowed attention for one layer.

    Consumes the post-RoPE Q and post-projection K/V, produces the same
    output tensor name the dense path fed into o_proj.
    """
    p = f"/model/layers.{layer}/attn"
    A = f"{p}/sparse"
    n = []

    q_rot = f"{p}/q_rotary/RotaryEmbedding/output_0"   # [B,N,896]
    k_rot = f"{p}/k_rotary/RotaryEmbedding/output_0"   # [B,N,128]
    v_lin = f"{p}/v_proj/Add/output_0"                 # [B,N,128]
    sinks = f"model.layers.{layer}.attn.sinks"         # [1,14,1,1]

    # Q: pad -> [B,NB,S,14,64] -> [B,NB,14,S,64] -> [B,NB,2,7S,64]
    n.append(helper.make_node("Pad", [q_rot, sh["pads_q"]], [f"{A}/q_pad"], name=f"{A}/q_pad"))
    n.append(helper.make_node("Reshape", [f"{A}/q_pad", c["q_blocks"]], [f"{A}/q_blocks"], name=f"{A}/q_blocks"))
    n.append(helper.make_node("Transpose", [f"{A}/q_blocks"], [f"{A}/q_heads"], name=f"{A}/q_heads",
                              perm=[0, 1, 3, 2, 4]))
    n.append(helper.make_node("Reshape", [f"{A}/q_heads", c["q_fold"]], [f"{A}/q_fold"], name=f"{A}/q_fold"))

    # K windows: pad -> [B,P,2,64] -> gather -> [B,NB,W,2,64] -> [B,NB,2,64,W]
    n.append(helper.make_node("Pad", [k_rot, sh["pads_kv"]], [f"{A}/k_pad"], name=f"{A}/k_pad"))
    n.append(helper.make_node("Reshape", [f"{A}/k_pad", c["kv_heads"]], [f"{A}/k_heads"], name=f"{A}/k_heads"))
    n.append(helper.make_node("Gather", [f"{A}/k_heads", sh["gather_idx"]], [f"{A}/k_win"], name=f"{A}/k_win", axis=1))
    n.append(helper.make_node("Transpose", [f"{A}/k_win"], [f"{A}/k_hw"], name=f"{A}/k_hw",
                              perm=[0, 1, 3, 2, 4]))
    n.append(helper.make_node("Transpose", [f"{A}/k_hw"], [f"{A}/k_t"], name=f"{A}/k_t",
                              perm=[0, 1, 2, 4, 3]))

    # scores [B,NB,2,7S,W] + band mask + padding mask
    n.append(helper.make_node("MatMul", [f"{A}/q_fold", f"{A}/k_t"], [f"{A}/scores"], name=f"{A}/scores"))
    n.append(helper.make_node("Add", [f"{A}/scores", sh["band_add5"]], [f"{A}/scores_band"], name=f"{A}/scores_band"))
    n.append(helper.make_node("Add", [f"{A}/scores_band", sh["mask_add5"]], [f"{A}/scores_mask"], name=f"{A}/scores_mask"))

    # sinks: [1,14,1,1] -> [1,1,2,7,1] -> [1,1,2,7,S] -> [1,1,2,7S,1] -> [B,NB,2,7S,1]
    n.append(helper.make_node("Reshape", [sinks, c["sinks_g"]], [f"{A}/sinks_g"], name=f"{A}/sinks_g"))
    n.append(helper.make_node("Expand", [f"{A}/sinks_g", c["sinks_S"]], [f"{A}/sinks_S"], name=f"{A}/sinks_S"))
    n.append(helper.make_node("Reshape", [f"{A}/sinks_S", c["sinks_flat"]], [f"{A}/sinks_flat"], name=f"{A}/sinks_flat"))
    n.append(helper.make_node("Expand", [f"{A}/sinks_flat", sh["sinks_shape"]], [f"{A}/sinks_col"], name=f"{A}/sinks_col"))

    n.append(helper.make_node("Concat", [f"{A}/scores_mask", f"{A}/sinks_col"], [f"{A}/scores_sink"],
                              name=f"{A}/scores_sink", axis=-1))
    n.append(helper.make_node("Softmax", [f"{A}/scores_sink"], [f"{A}/probs_full"], name=f"{A}/probs_full", axis=-1))
    n.append(helper.make_node("Slice", [f"{A}/probs_full", c["slice0"], c["sliceW"], c["axm1"]],
                              [f"{A}/probs"], name=f"{A}/probs"))

    # V windows and output
    n.append(helper.make_node("Pad", [v_lin, sh["pads_kv"]], [f"{A}/v_pad"], name=f"{A}/v_pad"))
    n.append(helper.make_node("Reshape", [f"{A}/v_pad", c["kv_heads"]], [f"{A}/v_heads"], name=f"{A}/v_heads"))
    n.append(helper.make_node("Gather", [f"{A}/v_heads", sh["gather_idx"]], [f"{A}/v_win"], name=f"{A}/v_win", axis=1))
    n.append(helper.make_node("Transpose", [f"{A}/v_win"], [f"{A}/v_hw"], name=f"{A}/v_hw",
                              perm=[0, 1, 3, 2, 4]))
    n.append(helper.make_node("MatMul", [f"{A}/probs", f"{A}/v_hw"], [f"{A}/av"], name=f"{A}/av"))

    # unfold: [B,NB,2,7S,64] -> [B,NB,2,7,S,64] -> [B,NB,S,2,7,64] -> [B,Np,896] -> [B,N,896]
    n.append(helper.make_node("Reshape", [f"{A}/av", c["av_unfold"]], [f"{A}/av_unfold"], name=f"{A}/av_unfold"))
    n.append(helper.make_node("Transpose", [f"{A}/av_unfold"], [f"{A}/av_seq"], name=f"{A}/av_seq",
                              perm=[0, 1, 4, 2, 3, 5]))
    n.append(helper.make_node("Reshape", [f"{A}/av_seq", c["out_flat"]], [f"{A}/out_flat"], name=f"{A}/out_flat"))
    n.append(helper.make_node("Slice", [f"{A}/out_flat", c["slice0"], sh["seq_n_1d"], c["ax1"]],
                              [f"{p}/out/reshape/output_0"], name=f"{A}/out_slice"))
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="_sparse_export/dense_original/model_quantized.onnx",
                    help="dense original graph (the shipped pre-rewrite model)")
    ap.add_argument("--dst", default="_sparse_export/out/model_quantized.onnx")
    args = ap.parse_args()

    model = onnx.load(args.src, load_external_data=False)
    g = model.graph

    c = {
        "zero": "/model/constants/INT64/0",
        "one": "/model/constants/INT64/1",
        "zerof": "/model/constants/FLOAT/0.0",
        "neg": "/model/constants/FLOAT/-3.4028234663852886e+38",
        "Sm1": "/model/sparse_attn/const/Sm1",
        "S": "/model/sparse_attn/const/S",
        "halo": "/model/sparse_attn/const/halo",
        "W": "/model/sparse_attn/const/W",
        "7S": "/model/sparse_attn/const/7S",
        "ax0": "/model/sparse_attn/const/ax0",
        "ax1": "/model/sparse_attn/const/ax1",
        "ax23": "/model/sparse_attn/const/ax23",
        "axm1": "/model/sparse_attn/const/axm1",
        "zero1": "/model/sparse_attn/const/zero1",
        "halo1": "/model/sparse_attn/const/halo1",
        "one1": "/model/sparse_attn/const/one1",
        "zeros3": "/model/sparse_attn/const/zeros3",
        "kv1": "/model/sparse_attn/const/kv1",
        "7S1": "/model/sparse_attn/const/7S1",
        "band5": "/model/sparse_attn/const/band5",
        "q_blocks": "/model/sparse_attn/const/q_blocks",
        "q_fold": "/model/sparse_attn/const/q_fold",
        "kv_heads": "/model/sparse_attn/const/kv_heads",
        "sinks_g": "/model/sparse_attn/const/sinks_g",
        "sinks_S": "/model/sparse_attn/const/sinks_S",
        "sinks_flat": "/model/sparse_attn/const/sinks_flat",
        "slice0": "/model/sparse_attn/const/slice0",
        "sliceW": "/model/sparse_attn/const/sliceW",
        "av_unfold": "/model/sparse_attn/const/av_unfold",
        "out_flat": "/model/sparse_attn/const/out_flat",
    }
    new_inits = [
        _const_i64(c["Sm1"], BLOCK - 1),
        _const_i64(c["S"], BLOCK),
        _const_i64(c["halo"], HALO),
        _const_i64(c["W"], WIN),
        _const_i64(c["7S"], ROWS),
        _const_i64(c["ax0"], [0]),
        _const_i64(c["ax1"], [1]),
        _const_i64(c["ax23"], [2, 3]),
        _const_i64(c["axm1"], [-1]),
        _const_i64(c["zero1"], [0]),
        _const_i64(c["halo1"], [HALO]),
        _const_i64(c["one1"], [1]),
        _const_i64(c["zeros3"], [0, 0, 0]),
        _const_i64(c["kv1"], [N_KVHEADS]),
        _const_i64(c["7S1"], [ROWS]),
        _const_i64(c["band5"], [1, 1, 1, ROWS, WIN]),
        _const_i64(c["q_blocks"], [0, -1, BLOCK, N_QHEADS, HEAD_DIM]),
        _const_i64(c["q_fold"], [0, 0, N_KVHEADS, -1, HEAD_DIM]),
        _const_i64(c["kv_heads"], [0, 0, N_KVHEADS, HEAD_DIM]),
        _const_i64(c["sinks_g"], [1, 1, N_KVHEADS, Q_MULT, 1]),
        _const_i64(c["sinks_S"], [1, 1, N_KVHEADS, Q_MULT, BLOCK]),
        _const_i64(c["sinks_flat"], [1, 1, N_KVHEADS, ROWS, 1]),
        _const_i64(c["slice0"], [0]),
        _const_i64(c["sliceW"], [WIN]),
        _const_i64(c["av_unfold"], [0, 0, N_KVHEADS, Q_MULT, BLOCK, HEAD_DIM]),
        _const_i64(c["out_flat"], [0, -1, ATTN_OUT]),
    ]

    # Nodes removed per layer: the whole dense attention core. The output of
    # out/reshape is regenerated by the sparse path under the same name.
    per_layer_suffixes = [
        "q/reshape_4d", "q/transpose",
        "k/reshape_4d", "k/transpose",
        "k/repeat_kv/unsqueeze", "k/repeat_kv/expand", "k/repeat_kv/reshape",
        "k/transpose_score",
        "v/reshape_4d", "v/transpose",
        "v/repeat_kv/unsqueeze", "v/repeat_kv/expand", "v/repeat_kv/reshape",
        "scores/MatMul", "scores/add_mask", "sinks/expand", "scores/cat_sink",
        "softmax", "softmax/slice",
        "out/MatMul", "out/transpose", "out/reshape",
    ]
    remove = set()
    for layer in range(8):
        for suf in per_layer_suffixes:
            remove.add(f"/model/layers.{layer}/attn/{suf}")
    # Shared quadratic mask and shape-plumbing nodes (dead once layers go).
    remove |= {
        "/model/local_sliding_window_mask/pos/row",
        "/model/local_sliding_window_mask/diff",
        "/model/local_sliding_window_mask/abs_diff",
        "/model/local_sliding_window_mask/in_band",
        "/model/local_sliding_window_mask/key_attended/Cast",
        "/model/local_sliding_window_mask/key_attended/4d",
        "/model/local_sliding_window_mask/bool_and",
        "/model/local_sliding_window_mask/where",
        "/model/shared_dims/attention_mask/Shape",
        "/model/shared_dims/attention_mask/Gather_0",
        "/model/shared_dims/attention_mask/Gather_1",
        "/model/shared_dims/attention_mask_batch_size_1d",
        "/model/shared_dims/attention_mask_seq_len_1d",
        "/model/shapes/repeat_kv_expand",
        "/model/shapes/repeat_kv_reshape",
        "/model/shapes/sinks_target",
    }

    kept = [node for node in g.node if node.name not in remove]
    unmatched = remove - set(node.name for node in g.node)
    if unmatched:
        raise RuntimeError(f"remove-list names not found in graph: {sorted(unmatched)}")

    del g.node[:]
    g.node.extend(kept)

    sh, shared_nodes = _build_shared_nodes(c)
    g.node.extend(shared_nodes)
    for layer in range(8):
        g.node.extend(_build_layer_nodes(layer, sh, c))
    g.initializer.extend(new_inits)

    # Reorder topologically (we appended nodes after their consumers' old
    # positions) and verify.
    _toposort(g)

    # onnx.checker insists on resolving external data files relative to the
    # cwd; skip it. The toposort above verifies every node input resolves,
    # and the downstream ORT session load is the real structural check.

    os.makedirs(os.path.dirname(args.dst), exist_ok=True)
    # Raw serialize: initializers keep their external-data references, so the
    # existing model_quantized.onnx_data stays valid byte-for-byte.
    with open(args.dst, "wb") as f:
        f.write(model.SerializeToString())
    print(f"wrote {args.dst} ({os.path.getsize(args.dst)} bytes), "
          f"{len(g.node)} nodes, external weights untouched")


def _toposort(g):
    """Stable topological sort of g.node in place."""
    produced = {}
    for idx, node in enumerate(g.node):
        for out in node.output:
            if out:
                produced[out] = idx
    init_names = {t.name for t in g.initializer}
    input_names = {i.name for i in g.input}
    available = set(init_names) | set(input_names)
    remaining = list(g.node)
    ordered = []
    while remaining:
        progressed = False
        still = []
        for node in remaining:
            deps = [i for i in node.input if i]
            if all(d in available for d in deps):
                ordered.append(node)
                for out in node.output:
                    if out:
                        available.add(out)
                progressed = True
            else:
                still.append(node)
        if not progressed:
            unresolved = {
                d for node in still for d in node.input
                if d and d not in available
            }
            raise RuntimeError(f"cannot toposort, unresolved inputs: {sorted(unresolved)[:10]}")
        remaining = still
    del g.node[:]
    g.node.extend(ordered)


if __name__ == "__main__":
    main()
