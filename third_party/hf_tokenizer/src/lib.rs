use std::ffi::{c_char, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use tokenizers::Tokenizer;

pub struct ArTokenizer {
    tokenizer: Tokenizer,
}

#[repr(C)]
pub struct ArEncoding {
    ids: *mut i64,
    starts: *mut usize,
    ends: *mut usize,
    token_offsets: *mut usize,
    token_text: *mut u8,
    token_text_len: usize,
    len: usize,
}

#[repr(C)]
pub struct ArBuffer {
    data: *mut u8,
    len: usize,
}

fn into_raw_slice<T>(values: Vec<T>) -> *mut T {
    Box::into_raw(values.into_boxed_slice()) as *mut T
}

unsafe fn free_raw_slice<T>(pointer: *mut T, len: usize) {
    if !pointer.is_null() {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(pointer, len)));
    }
}

#[no_mangle]
pub extern "C" fn ar_tokenizer_create(path: *const c_char) -> *mut ArTokenizer {
    if path.is_null() {
        return ptr::null_mut();
    }
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(path) => path,
        Err(_) => return ptr::null_mut(),
    };
    catch_unwind(AssertUnwindSafe(|| {
        Tokenizer::from_file(path)
            .ok()
            .map(|tokenizer| Box::into_raw(Box::new(ArTokenizer { tokenizer })))
            .unwrap_or(ptr::null_mut())
    }))
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn ar_tokenizer_destroy(tokenizer: *mut ArTokenizer) {
    if !tokenizer.is_null() {
        drop(Box::from_raw(tokenizer));
    }
}

#[no_mangle]
pub unsafe extern "C" fn ar_tokenizer_vocab_size(tokenizer: *const ArTokenizer) -> usize {
    if tokenizer.is_null() {
        return 0;
    }
    (*tokenizer).tokenizer.get_vocab_size(true)
}

#[no_mangle]
pub unsafe extern "C" fn ar_tokenizer_encode(
    tokenizer: *const ArTokenizer,
    text: *const u8,
    text_len: usize,
    output: *mut *mut ArEncoding,
) -> i32 {
    if tokenizer.is_null() || text.is_null() || output.is_null() {
        return 1;
    }
    *output = ptr::null_mut();
    let text = match std::str::from_utf8(std::slice::from_raw_parts(text, text_len)) {
        Ok(text) => text,
        Err(_) => return 2,
    };
    let result = catch_unwind(AssertUnwindSafe(|| {
        (*tokenizer).tokenizer.encode(text, true).map_err(|_| ())
    }));
    let encoding = match result {
        Ok(Ok(encoding)) => encoding,
        _ => return 3,
    };
    let len = encoding.len();
    let ids = into_raw_slice(encoding.get_ids().iter().map(|id| i64::from(*id)).collect());
    let starts = into_raw_slice(
        encoding
            .get_offsets()
            .iter()
            .map(|offset| offset.0)
            .collect(),
    );
    let ends = into_raw_slice(
        encoding
            .get_offsets()
            .iter()
            .map(|offset| offset.1)
            .collect(),
    );
    let mut token_offsets = Vec::with_capacity(len + 1);
    let mut token_text = Vec::new();
    token_offsets.push(0);
    for token in encoding.get_tokens() {
        token_text.extend_from_slice(token.as_bytes());
        token_offsets.push(token_text.len());
    }
    let token_offsets = into_raw_slice(token_offsets);
    let token_text_len = token_text.len();
    let token_text = into_raw_slice(token_text);
    *output = Box::into_raw(Box::new(ArEncoding {
        ids,
        starts,
        ends,
        token_offsets,
        token_text,
        token_text_len,
        len,
    }));
    0
}

#[no_mangle]
pub unsafe extern "C" fn ar_encoding_free(encoding: *mut ArEncoding) {
    if encoding.is_null() {
        return;
    }
    let encoding = Box::from_raw(encoding);
    free_raw_slice(encoding.ids, encoding.len);
    free_raw_slice(encoding.starts, encoding.len);
    free_raw_slice(encoding.ends, encoding.len);
    free_raw_slice(encoding.token_offsets, encoding.len + 1);
    free_raw_slice(encoding.token_text, encoding.token_text_len);
}

#[no_mangle]
pub unsafe extern "C" fn ar_tokenizer_decode(
    tokenizer: *const ArTokenizer,
    ids: *const i64,
    len: usize,
    output: *mut ArBuffer,
) -> i32 {
    if tokenizer.is_null() || ids.is_null() || output.is_null() {
        return 1;
    }
    let ids = std::slice::from_raw_parts(ids, len);
    if ids.iter().any(|id| *id < 0 || *id > i64::from(u32::MAX)) {
        return 2;
    }
    let ids: Vec<u32> = ids.iter().map(|id| *id as u32).collect();
    let result = catch_unwind(AssertUnwindSafe(|| {
        (*tokenizer).tokenizer.decode(&ids, false).map_err(|_| ())
    }));
    let decoded = match result {
        Ok(Ok(decoded)) => decoded.into_bytes(),
        _ => return 3,
    };
    let len = decoded.len();
    (*output).len = len;
    (*output).data = into_raw_slice(decoded);
    0
}

#[no_mangle]
pub unsafe extern "C" fn ar_buffer_free(buffer: *mut ArBuffer) {
    if buffer.is_null() {
        return;
    }
    let buffer = &mut *buffer;
    free_raw_slice(buffer.data, buffer.len);
    buffer.data = ptr::null_mut();
    buffer.len = 0;
}
