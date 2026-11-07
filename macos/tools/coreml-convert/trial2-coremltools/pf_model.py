import torch
import torch.nn as nn
import torch.nn.functional as F


def rms(x, w):
    return x * torch.rsqrt(x.pow(2).mean(-1, keepdim=True) + 9.999999747378752e-06) * w


def rope(x, cos_cache, sin_cache, nheads: int):
    # x [B, L, nheads*64]; positions = arange(L); interleaved rotary pairs
    B, L, D = x.shape
    xh = x.view(B, L, nheads, 64)
    xf = xh.view(B, L, nheads, 32, 2)
    x1 = xf[..., 0]
    x2 = xf[..., 1]
    c = cos_cache[:L].view(1, L, 1, 32)
    s = sin_cache[:L].view(1, L, 1, 32)
    o1 = x1 * c - x2 * s
    o2 = x2 * c + x1 * s
    return torch.stack((o1, o2), dim=-1).view(B, L, nheads * 64)


class MoE(nn.Module):
    def __init__(self, rw, rb, gu, gub, dw, db):
        super().__init__()
        self.rw = nn.Parameter(rw)
        self.rb = nn.Parameter(rb)
        self.gu = nn.Parameter(gu)
        self.gub = nn.Parameter(gub)
        self.dw = nn.Parameter(dw)
        self.db = nn.Parameter(db)

    def expert(self, x, e):
        guw = self.gu.index_select(0, e)
        gub = self.gub.index_select(0, e)
        gu = torch.bmm(x.unsqueeze(1), guw.transpose(1, 2)).squeeze(1) + gub
        gate = gu[:, 0::2]
        lin = gu[:, 1::2]
        # verified vs ORT QMoE CPU kernel: g*sigmoid(1.702*g)*(clip(lin,-7,7)+1)
        g = torch.clamp(gate, max=7.0)
        h = g * torch.sigmoid(1.702 * g) * (torch.clamp(lin, -7.0, 7.0) + 1.0)
        dww = self.dw.index_select(0, e)
        db = self.db.index_select(0, e)
        return torch.bmm(h.unsqueeze(1), dww.transpose(1, 2)).squeeze(1) + db

    def forward(self, x, B: int, L: int):
        xr = x.reshape(-1, 640)
        router = xr @ self.rw.t() + self.rb
        vals, idx = torch.topk(router, 4, dim=-1)
        rw_ = torch.softmax(vals, dim=-1)  # QMoE routing: topk then softmax over top-k
        out = torch.zeros_like(xr)
        for k in range(4):
            y = self.expert(xr, idx[:, k])
            out = out + rw_[:, k:k + 1] * y
        return out.view(B, L, 640)


class Layer(nn.Module):
    def __init__(self, w, cos, sin):
        super().__init__()
        self.q = nn.Parameter(w["q"]); self.qb = nn.Parameter(w["qb"])
        self.k = nn.Parameter(w["k"]); self.kb = nn.Parameter(w["kb"])
        self.v = nn.Parameter(w["v"]); self.vb = nn.Parameter(w["vb"])
        self.o = nn.Parameter(w["o"]); self.ob = nn.Parameter(w["ob"])
        self.ln1 = nn.Parameter(w["ln1"])
        self.ln2 = nn.Parameter(w["ln2"])
        self.sink = nn.Parameter(w["sink"])
        self.moe = MoE(w["rw"], w["rb"], w["gu"], w["gub"], w["dw"], w["db"])
        self.register_buffer("cos", cos, persistent=False)
        self.register_buffer("sin", sin, persistent=False)
        self.register_buffer("pen", torch.full((1,), 6.0e4, dtype=self.q.dtype), persistent=False)

    def forward(self, x, am, B: int, L: int):
        h = rms(x, self.ln1)
        q = rope(h @ self.q.t() + self.qb, self.cos, self.sin, 14).view(B, L, 14, 64)
        k = rope(h @ self.k.t() + self.kb, self.cos, self.sin, 2).view(B, L, 2, 64)
        v = (h @ self.v.t() + self.vb).view(B, L, 2, 64)

        nb = (L + 255) // 256
        npad = nb * 256
        pad = npad - L
        halo = 128

        qp = F.pad(q, (0, 0, 0, 0, 0, pad)).view(B, nb, 256, 14, 64)
        kp = F.pad(k, (0, 0, 0, 0, halo, pad + halo)).view(B, npad + 2 * halo, 2, 64)
        vp = F.pad(v, (0, 0, 0, 0, halo, pad + halo)).view(B, npad + 2 * halo, 2, 64)
        amp = F.pad(am, (halo, pad + halo))

        idx = (torch.arange(nb) * 256).view(-1, 1) + torch.arange(512).view(1, -1)
        idxf = idx.reshape(-1)
        k_win = kp[:, idxf].view(B, nb, 512, 2, 64).transpose(2, 3)
        v_win = vp[:, idxf].view(B, nb, 512, 2, 64).transpose(2, 3)

        qf = qp.view(B, nb, 256, 2, 7, 64).permute(0, 1, 3, 4, 2, 5).reshape(B, nb, 2, 7 * 256, 64)
        scores = torch.matmul(qf, k_win.transpose(-1, -2))

        rows = torch.arange(256)
        cols = torch.arange(512)
        band = (cols.view(1, -1) >= rows.view(-1, 1)) & (cols.view(1, -1) <= rows.view(-1, 1) + 256)
        band_add = (band.to(scores.dtype) * self.pen - self.pen)
        band_add = band_add.view(1, 1, 1, 1, 256, 512).expand(1, 1, 1, 7, 256, 512).reshape(1, 1, 1, 7 * 256, 512)
        key_bad = (amp[:, idxf] == 0).view(B, nb, 1, 1, 512)
        mask_add = key_bad.to(scores.dtype) * (-self.pen)

        sink = self.sink.view(1, 1, 2, 7, 1, 1).expand(1, nb, 2, 7, 256, 1).reshape(1, nb, 2, 7 * 256, 1).expand(B, -1, -1, -1, -1)
        scores_sink = torch.cat([scores + band_add + mask_add, sink], dim=-1)
        probs = torch.softmax(scores_sink, dim=-1)[..., :512]

        av = torch.matmul(probs, v_win)
        av = av.view(B, nb, 2, 7, 256, 64).permute(0, 1, 4, 2, 3, 5).reshape(B, npad, 14, 64)[:, :L]
        attn_out = av.reshape(B, L, 14 * 64) @ self.o.t() + self.ob

        res = x + attn_out
        y = rms(res, self.ln2)
        y = self.moe(y, B, L)
        return res + y


class Net(nn.Module):
    def __init__(self, weights):
        super().__init__()
        self.embed = nn.Parameter(weights["embed"])
        self.register_buffer("cos", weights["cos_cache"], persistent=False)
        self.register_buffer("sin", weights["sin_cache"], persistent=False)
        self.layers = nn.ModuleList([
            Layer({k.split(".", 1)[1]: v for k, v in weights.items() if k.startswith(f"L{i}.")}, self.cos, self.sin)
            for i in range(8)
        ])
        self.lnf = nn.Parameter(weights["lnf"])
        self.score = nn.Parameter(weights["score"])

    def forward(self, input_ids, attention_mask, B: int, L: int):
        am = attention_mask.to(torch.float32)
        x = F.embedding(input_ids, self.embed)
        for layer in self.layers:
            x = layer(x, am, B, L)
        y = rms(x, self.lnf)
        return y @ self.score.t()
