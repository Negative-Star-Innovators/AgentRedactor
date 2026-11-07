import numpy as np
import onnx
import onnx.numpy_helper as nh
import time

SRC = "/Users/ursusprizlius/Documents/Projects/AgentRedactor/macos/build/engine/models/onnx/model_quantized.onnx"
OUT = "/tmp/coreml-exp/weights.npz"

t0 = time.time()
m = onnx.load(SRC, load_external_data=True)
g = m.graph
INIT = {init.name: nh.to_array(init) for init in g.initializer}
print(f"loaded in {time.time()-t0:.1f}s, {len(INIT)} initializers")

def dequant_nbits(name):
    # MatMulNBits: quant [N, K/32, 32] uint8, scales [N, K/32], zero_points [N, K/32]
    qk = INIT[f"{name}_weight_quant"].astype(np.int32)
    sc = INIT[f"{name}_weight_scales"]
    z = INIT[f"{name}_weight_zp"].astype(np.int32)
    N = qk.shape[0]
    return ((qk - z[:, :, None]) * sc[:, :, None]).reshape(N, -1).astype(np.float32)

def dequant_sym(name):
    # QMoE experts: quant [E, rows, cols] uint8 storing int8+128; scales [E, rows, cols/32]; implicit zp=128
    qk = INIT[f"{name}_quant"].astype(np.int32)
    sc = INIT[f"{name}_scales"]
    shape = qk.shape
    return ((qk - 128).reshape(*shape[:-1], shape[-1] // 32, 32) * sc[..., None]).reshape(*shape).astype(np.float32)

W = {}
# embedding (GatherBlockQuantized, block 32 over last dim, with explicit zp)
q = INIT["model_embed_tokens_weight_quant"].astype(np.int32)
s = INIT["model_embed_tokens_weight_scales"]
zp = INIT["model_embed_tokens_weight_zp"].astype(np.int32)
V, K = q.shape
W["embed"] = ((q.reshape(V, K // 32, 32) - zp[..., None]) * s[..., None]).reshape(V, K).astype(np.float32)
W["cos_cache"] = INIT["cos_cache"]
W["sin_cache"] = INIT["sin_cache"]

for i in range(8):
    p = f"model_layers_{i}"
    W[f"L{i}.q"] = dequant_nbits(f"{p}_attn_q_proj_MatMul")
    W[f"L{i}.k"] = dequant_nbits(f"{p}_attn_k_proj_MatMul")
    W[f"L{i}.v"] = dequant_nbits(f"{p}_attn_v_proj_MatMul")
    W[f"L{i}.o"] = dequant_nbits(f"{p}_attn_o_proj_MatMul")
    W[f"L{i}.qb"] = INIT[f"model.layers.{i}.attn.q_proj.Add.bias"]
    W[f"L{i}.kb"] = INIT[f"model.layers.{i}.attn.k_proj.Add.bias"]
    W[f"L{i}.vb"] = INIT[f"model.layers.{i}.attn.v_proj.Add.bias"]
    W[f"L{i}.ob"] = INIT[f"model.layers.{i}.attn.o_proj.Add.bias"]
    W[f"L{i}.ln1"] = INIT[f"model.layers.{i}.input_layernorm.weight"]
    W[f"L{i}.ln2"] = INIT[f"model.layers.{i}.post_attention_layernorm.weight"]
    W[f"L{i}.sink"] = INIT[f"model.layers.{i}.attn.sinks"].reshape(-1)
    W[f"L{i}.rw"] = dequant_nbits(f"{p}_moe_router_MatMul")
    W[f"L{i}.rb"] = INIT[f"model.layers.{i}.moe.router.Add.bias"]
    W[f"L{i}.gu"] = dequant_sym(f"{p}_moe_experts_gate_up_proj_weight")
    W[f"L{i}.gub"] = INIT[f"model.layers.{i}.moe.experts.gate_up_proj.bias"]
    W[f"L{i}.dw"] = dequant_sym(f"{p}_moe_experts_down_proj_weight")
    W[f"L{i}.db"] = INIT[f"model.layers.{i}.moe.experts.down_proj.bias"]
    print(f"layer {i} done", flush=True)

W["lnf"] = INIT["model.layers.8.final_norm_layernorm.weight"]
W["score"] = dequant_nbits("model_score_MatMul")

t0 = time.time()
np.savez(OUT, **W)
print(f"saved {OUT} in {time.time()-t0:.1f}s, total GB:", sum(v.nbytes for v in W.values()) / 1e9)
