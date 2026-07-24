# Model Files Required

This directory must contain the OpenAI Privacy Filter model files.

## Required Files

Place all files directly in this `models/` directory (or in `models/onnx/` for the ONNX files):

- `config.json` — Model configuration (contains id2label mapping)
- `viterbi_calibration.json` — CRF decoder calibration biases
- `tokenizer.json` — BPE tokenizer vocabulary and merges (~278MB)
- `onnx/model_quantized.onnx` — Quantized ONNX model
- `onnx/model_quantized.onnx_data` — External model data (if split)

## Download Instructions

### Option 1: Using wget

```bash
cd models
mkdir -p onnx

wget https://huggingface.co/openai/privacy-filter/resolve/main/config.json
wget https://huggingface.co/openai/privacy-filter/resolve/main/viterbi_calibration.json
wget https://huggingface.co/openai/privacy-filter/resolve/main/tokenizer.json
wget https://huggingface.co/openai/privacy-filter/resolve/main/onnx/model_quantized.onnx -O onnx/model_quantized.onnx
wget https://huggingface.co/openai/privacy-filter/resolve/main/onnx/model_quantized.onnx_data -O onnx/model_quantized.onnx_data
```

### Option 2: Using curl

```bash
cd models
mkdir -p onnx

curl -L -o config.json https://huggingface.co/openai/privacy-filter/resolve/main/config.json
curl -L -o viterbi_calibration.json https://huggingface.co/openai/privacy-filter/resolve/main/viterbi_calibration.json
curl -L -o tokenizer.json https://huggingface.co/openai/privacy-filter/resolve/main/tokenizer.json
curl -L -o onnx/model_quantized.onnx https://huggingface.co/openai/privacy-filter/resolve/main/onnx/model_quantized.onnx
curl -L -o onnx/model_quantized.onnx_data https://huggingface.co/openai/privacy-filter/resolve/main/onnx/model_quantized.onnx_data
```

### Option 3: Manual download

Visit https://huggingface.co/openai/privacy-filter/tree/main and download the files listed above.

## Notes

- The `tokenizer.json` is approximately 278MB due to the large o200k_base vocabulary (199,998 tokens) and 446,189 BPE merge rules.
- The quantized ONNX model is the recommended version for CPU/GPU inference.
- If `model_quantized.onnx_data` does not exist (model not split), only `model_quantized.onnx` is needed.
