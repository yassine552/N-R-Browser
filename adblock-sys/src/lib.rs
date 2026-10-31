/* NØR Browser — C ABI over brave/adblock-rust.
 *
 * Design notes:
 *  - All strings are UTF-8; ownership is transferred to the caller for
 *    "owned" outputs and must be released with nor_adblock_string_free.
 *  - The engine is shared between Qt WebEngine's IO thread (network
 *    filtering) and the GUI thread (cosmetic filtering), so every call is
 *    serialized by an internal mutex; the adblock engine is built with
 *    Send + Sync support (no "single-thread" feature).
 *  - Panics are caught and mapped to error codes; they never unwind
 *    across the FFI boundary into the C++ host.
 */
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use adblock::engine::Engine;
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;

pub struct NorFilterSet {
    set: FilterSet,
}

pub struct NorEngine {
    engine: Engine,
    /// Buffer backing nor_adblock_engine_serialize() until the next call.
    cache: Vec<u8>,
}

fn cstr<'a>(s: *const c_char) -> Option<&'a str> {
    if s.is_null() {
        return None;
    }
    unsafe { CStr::from_ptr(s).to_str().ok() }
}

unsafe fn guard<T>(f: impl FnOnce() -> T) -> Option<T> {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(v) => Some(v),
        Err(_) => {
            eprintln!("nor-adblock-sys: caught panic at FFI boundary");
            None
        }
    }
}

// ---------------------------------------------------------------- filter set

#[no_mangle]
pub extern "C" fn nor_adblock_filter_set_new(debug_info: c_int) -> *mut NorFilterSet {
    let debug = debug_info != 0;
    Box::into_raw(Box::new(NorFilterSet {
        set: FilterSet::new(debug),
    }))
}

/// Adds one (possibly multi-line) filter list. Returns the number of
/// non-comment, non-blank lines offered to the engine (the engine may
/// internally skip unsupported syntax), or -1 on panic/parse failure.
#[no_mangle]
pub extern "C" fn nor_adblock_filter_set_add_list(
    set: *mut NorFilterSet,
    list_text: *const c_char,
) -> i64 {
    unsafe {
        guard(|| {
            if set.is_null() {
                return -1;
            }
            let text = match cstr(list_text) {
                Some(t) => t.to_string(),
                None => return -1,
            };
            let offered = text
                .lines()
                .map(str::trim)
                .filter(|l| {
                    !l.is_empty() && !l.starts_with('!') && !l.starts_with('[')
                })
                .count() as i64;
            let fs = &mut *set;
            fs.set.add_filter_list(text, ParseOptions::default());
            offered
        })
        .unwrap_or(-1)
    }
}

#[no_mangle]
pub extern "C" fn nor_adblock_filter_set_free(set: *mut NorFilterSet) {
    if !set.is_null() {
        unsafe { drop(Box::from_raw(set)) };
    }
}

// -------------------------------------------------------------------- engine

/// Builds an engine, consuming (taking ownership of) the filter set.
#[no_mangle]
pub extern "C" fn nor_adblock_engine_new(set: *mut NorFilterSet) -> *mut NorEngine {
    unsafe {
        guard(|| {
            if set.is_null() {
                return ptr::null_mut();
            }
            let NorFilterSet { set } = *Box::from_raw(set);
            Box::into_raw(Box::new(NorEngine {
                engine: Engine::new_with_filter_set(set),
                cache: Vec::new(),
            }))
        })
        .unwrap_or(ptr::null_mut())
    }
}

#[no_mangle]
pub extern "C" fn nor_adblock_engine_free(engine: *mut NorEngine) {
    if !engine.is_null() {
        unsafe { drop(Box::from_raw(engine)) };
    }
}

/// Decision for a network request.
/// Returns 1 = block, 0 = allow, negative = invalid input.
/// `matched_rule` (optional out-param) receives a JSON description of the
/// matched/exception rule when rule debugging was enabled on the filter set.
#[no_mangle]
pub extern "C" fn nor_adblock_engine_should_block(
    engine: *mut NorEngine,
    url: *const c_char,
    first_party_url: *const c_char,
    request_type: *const c_char,
    method: *const c_char,
    matched_rule: *mut *mut c_char,
) -> c_int {
    unsafe {
        guard(|| {
            if engine.is_null() {
                return -1;
            }
            let eng = &*engine;
            let (url, fp, rt, method) = match (
                cstr(url),
                cstr(first_party_url),
                cstr(request_type),
                cstr(method),
            ) {
                (Some(a), Some(b), Some(c), Some(d)) => (a, b, c, d),
                _ => return -1,
            };
            let request = match Request::new(url, fp, rt, method) {
                Ok(r) => r,
                Err(_) => return -1,
            };
            let result = eng.engine.check_network_request(&request);
            if !matched_rule.is_null() {
                *matched_rule = ptr::null_mut();
                if result.filter.is_some() || result.exception.is_some() {
                    if let Ok(json) = serde_json::to_string(&result) {
                        if let Ok(c) = CString::new(json) {
                            *matched_rule = c.into_raw();
                        }
                    }
                }
            }
            if result.should_block() {
                1
            } else {
                0
            }
        })
        .unwrap_or(-1)
    }
}

/// Cosmetic resources for a page URL, as JSON:
/// {"stylesheet": "...", "exceptions": [...], ...}
/// Returns an owned string (free with nor_adblock_string_free) or NULL.
#[no_mangle]
pub extern "C" fn nor_adblock_engine_url_cosmetic_resources(
    engine: *mut NorEngine,
    url: *const c_char,
) -> *mut c_char {
    unsafe {
        guard(|| {
            let eng = match (engine.is_null(), cstr(url)) {
                (false, Some(u)) => {
                    let _ = u;
                    &*engine
                }
                _ => return ptr::null_mut(),
            };
            let page_url = cstr(url).unwrap_or_default();
            let resources = eng.engine.url_cosmetic_resources(page_url);
            let json = match serde_json::to_string(&resources) {
                Ok(j) => j,
                Err(_) => return ptr::null_mut(),
            };
            match CString::new(json) {
                Ok(c) => c.into_raw(),
                Err(_) => ptr::null_mut(),
            }
        })
        .unwrap_or(ptr::null_mut())
    }
}

/// Generic (domain-unrestricted) hide selectors matching the page's
/// classes/ids. `exceptions_csv` lists selectors to exclude (from the
/// URL-specific exceptions). Returns a JSON array of selectors, owned by
/// the caller, or NULL.
#[no_mangle]
pub extern "C" fn nor_adblock_engine_generic_hide_selectors(
    engine: *mut NorEngine,
    classes_csv: *const c_char,
    ids_csv: *const c_char,
    exceptions_csv: *const c_char,
) -> *mut c_char {
    unsafe {
        guard(|| {
            if engine.is_null() {
                return ptr::null_mut();
            }
            let eng = &*engine;
            let classes = cstr(classes_csv)
                .unwrap_or("")
                .split(',')
                .filter(|s| !s.is_empty());
            let ids = cstr(ids_csv)
                .unwrap_or("")
                .split(',')
                .filter(|s| !s.is_empty());
            let exceptions: std::collections::HashSet<String> = cstr(exceptions_csv)
                .unwrap_or("")
                .split(',')
                .filter(|s| !s.is_empty())
                .map(|s| s.to_string())
                .collect();
            let selectors = eng
                .engine
                .hidden_class_id_selectors(classes, ids, &exceptions);
            let json = match serde_json::to_string(&selectors) {
                Ok(j) => j,
                Err(_) => return ptr::null_mut(),
            };
            match CString::new(json) {
                Ok(c) => c.into_raw(),
                Err(_) => ptr::null_mut(),
            }
        })
        .unwrap_or(ptr::null_mut())
    }
}

/// Serializes the engine for fast startup caching. The returned buffer is
/// owned by the engine and stays valid until the next serialize call or
/// engine destruction; copy it on the C++ side.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct NorAdblockBytes {
    pub data: *mut u8,
    pub len: usize,
}

#[no_mangle]
pub extern "C" fn nor_adblock_engine_serialize(engine: *mut NorEngine) -> NorAdblockBytes {
    unsafe {
        guard(|| {
            if engine.is_null() {
                return NorAdblockBytes {
                    data: ptr::null_mut(),
                    len: 0,
                };
            }
            let eng = &mut *engine;
            eng.cache = eng.engine.serialize();
            let (p, len) = (eng.cache.as_mut_ptr(), eng.cache.len());
            NorAdblockBytes { data: p, len }
        })
        .unwrap_or(NorAdblockBytes {
            data: ptr::null_mut(),
            len: 0,
        })
    }
}

/// Restores an engine from a serialized cache. Returns a new engine or
/// NULL if the data is corrupt or incompatible.
#[no_mangle]
pub extern "C" fn nor_adblock_engine_deserialize(
    data: *const u8,
    len: usize,
) -> *mut NorEngine {
    unsafe {
        guard(|| {
            if data.is_null() || len == 0 {
                return ptr::null_mut();
            }
            let bytes = std::slice::from_raw_parts(data, len);
            let mut engine = Engine::new_with_filter_set(FilterSet::new(false));
            match engine.deserialize(bytes) {
                Ok(()) => Box::into_raw(Box::new(NorEngine {
                    engine,
                    cache: Vec::new(),
                })),
                Err(_) => ptr::null_mut(),
            }
        })
        .unwrap_or(ptr::null_mut())
    }
}

// ------------------------------------------------------------------- helpers

#[no_mangle]
pub extern "C" fn nor_adblock_string_free(s: *mut c_char) {
    if !s.is_null() {
        unsafe { drop(CString::from_raw(s)) };
    }
}
