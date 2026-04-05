/*!
 * tempmailg.com：无 Cookie 罐会话 + POST /public/get_messages
 */

use std::collections::BTreeMap;
use std::sync::LazyLock;

use base64::{engine::general_purpose::STANDARD as B64, Engine as _};
use regex::Regex;
use serde::{Deserialize, Serialize};
use wreq::header::HeaderMap;

use crate::config::{block_on, get_current_ua, http_client_no_cookie_jar};
use crate::normalize::normalize_email;
use crate::types::{Channel, Email, EmailInfo};

const ORIGIN: &str = "https://tempmailg.com";
const TOK_PREFIX: &str = "tmg1:";

static CSRF_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"(?i)<meta\s+name="csrf-token"\s+content="([^"]+)""#).expect("csrf re")
});

#[derive(Serialize, Deserialize)]
struct TempmailgSess {
    l: String,
    c: String,
    s: String,
}

fn locale_from_domain(domain: Option<&str>) -> String {
    let s = domain.unwrap_or("").trim();
    if s.is_empty() || s.contains('/') || s.contains('?') || s.contains('#') || s.contains('\\') {
        return "zh".into();
    }
    s.to_string()
}

fn cookie_map(hdr: &str) -> BTreeMap<String, String> {
    let mut m = BTreeMap::new();
    for part in hdr.split(';') {
        let p = part.trim();
        if let Some(i) = p.find('=') {
            let k = p[..i].trim().to_string();
            let v = p[i + 1..].trim().to_string();
            if !k.is_empty() {
                m.insert(k, v);
            }
        }
    }
    m
}

fn cookie_hdr_from_map(m: &BTreeMap<String, String>) -> String {
    m.iter()
        .map(|(k, v)| format!("{}={}", k, v))
        .collect::<Vec<_>>()
        .join("; ")
}

fn merge_set_cookies(prev: &str, headers: &HeaderMap) -> String {
    let mut m = cookie_map(prev);
    for v in headers.get_all("set-cookie") {
        let s = v.to_str().unwrap_or("");
        let nv = s.split(';').next().unwrap_or("").trim();
        if let Some(i) = nv.find('=') {
            let k = nv[..i].trim().to_string();
            let val = nv[i + 1..].trim().to_string();
            if !k.is_empty() {
                m.insert(k, val);
            }
        }
    }
    cookie_hdr_from_map(&m)
}

fn xsrf_from_cookie_hdr(hdr: &str) -> String {
    let m = cookie_map(hdr);
    for key in ["XSRF-TOKEN", "xsrf-token"] {
        if let Some(v) = m.get(key) {
            return v.clone();
        }
    }
    for (k, v) in &m {
        if k.eq_ignore_ascii_case("xsrf-token") {
            return v.clone();
        }
    }
    String::new()
}

fn parse_csrf(html: &str) -> Result<String, String> {
    let cap = CSRF_RE
        .captures(html)
        .ok_or_else(|| "tempmailg: csrf-token not found in page".to_string())?;
    let t = cap.get(1).unwrap().as_str().trim();
    if t.is_empty() {
        return Err("tempmailg: empty csrf-token".into());
    }
    Ok(t.to_string())
}

fn encode_token(sess: &TempmailgSess) -> Result<String, String> {
    let json = serde_json::to_string(sess).map_err(|e| e.to_string())?;
    Ok(format!("{}{}", TOK_PREFIX, B64.encode(json)))
}

fn decode_token(tok: &str) -> Result<TempmailgSess, String> {
    if !tok.starts_with(TOK_PREFIX) {
        return Err("tempmailg: invalid session token".into());
    }
    let raw = B64
        .decode(tok.strip_prefix(TOK_PREFIX).unwrap())
        .map_err(|_| "tempmailg: invalid session token".to_string())?;
    let sess: TempmailgSess =
        serde_json::from_slice(&raw).map_err(|_| "tempmailg: invalid session token".to_string())?;
    if sess.c.is_empty() || sess.s.is_empty() {
        return Err("tempmailg: invalid session token".into());
    }
    Ok(sess)
}

pub fn generate_email(domain: Option<&str>) -> Result<EmailInfo, String> {
    let locale = locale_from_domain(domain);
    let page_url = format!("{ORIGIN}/public/{}", urlencoding::encode(&locale));
    block_on(async {
        let client = http_client_no_cookie_jar();
        let resp = client
            .get(&page_url)
            .header("User-Agent", get_current_ua())
            .header(
                "Accept",
                "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
            )
            .header("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8")
            .header("Cache-Control", "no-cache")
            .header("DNT", "1")
            .header("Pragma", "no-cache")
            .header("Referer", &page_url)
            .header("Upgrade-Insecure-Requests", "1")
            .send()
            .await
            .map_err(|e| format!("tempmailg page: {}", e))?;
        if !resp.status().is_success() {
            return Err(format!("tempmailg page: {}", resp.status()));
        }
        let headers = resp.headers().clone();
        let html = resp.text().await.map_err(|e| e.to_string())?;
        let csrf = parse_csrf(&html)?;
        let mut cookie_hdr = merge_set_cookies("", &headers);
        let xsrf = xsrf_from_cookie_hdr(&cookie_hdr);
        if xsrf.is_empty() {
            return Err("tempmailg: missing XSRF-TOKEN cookie".into());
        }

        let post_url = format!("{ORIGIN}/public/get_messages");
        let body = serde_json::to_string(&serde_json::json!({ "_token": csrf }))
            .map_err(|e| e.to_string())?;
        let resp2 = client
            .post(&post_url)
            .header("User-Agent", get_current_ua())
            .header("Accept", "application/json, text/plain, */*")
            .header("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8")
            .header("Content-Type", "application/json")
            .header("Origin", ORIGIN)
            .header("Referer", &page_url)
            .header("Cache-Control", "no-cache")
            .header("Pragma", "no-cache")
            .header("DNT", "1")
            .header("Cookie", &cookie_hdr)
            .header("X-XSRF-TOKEN", &xsrf)
            .body(body)
            .send()
            .await
            .map_err(|e| format!("tempmailg get_messages: {}", e))?;
        if !resp2.status().is_success() {
            return Err(format!("tempmailg get_messages: {}", resp2.status()));
        }
        let headers2 = resp2.headers().clone();
        let api_body = resp2.text().await.map_err(|e| e.to_string())?;
        let wrap: serde_json::Value =
            serde_json::from_str(&api_body).map_err(|e| format!("tempmailg json: {}", e))?;
        let ok = wrap.get("status").and_then(|x| x.as_bool()) == Some(true);
        let mailbox = wrap
            .get("mailbox")
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string();
        if !ok || mailbox.is_empty() {
            return Err("tempmailg: create mailbox failed".into());
        }
        cookie_hdr = merge_set_cookies(&cookie_hdr, &headers2);
        let tok = encode_token(&TempmailgSess {
            l: locale,
            c: cookie_hdr,
            s: csrf,
        })?;
        Ok(EmailInfo {
            channel: Channel::Tempmailg,
            email: mailbox,
            token: Some(tok),
            expires_at: None,
            created_at: None,
        })
    })
}

pub fn get_emails(token: &str, email: &str) -> Result<Vec<Email>, String> {
    let sess = decode_token(token)?;
    let locale = if sess.l.is_empty() {
        "zh".to_string()
    } else {
        sess.l.clone()
    };
    let page_url = format!("{ORIGIN}/public/{}", urlencoding::encode(&locale));
    let post_url = format!("{ORIGIN}/public/get_messages");
    let xsrf = xsrf_from_cookie_hdr(&sess.c);
    let body = serde_json::to_string(&serde_json::json!({ "_token": sess.s })).map_err(|e| e.to_string())?;
    block_on(async {
        let client = http_client_no_cookie_jar();
        let resp = client
            .post(&post_url)
            .header("User-Agent", get_current_ua())
            .header("Accept", "application/json, text/plain, */*")
            .header("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8")
            .header("Content-Type", "application/json")
            .header("Origin", ORIGIN)
            .header("Referer", &page_url)
            .header("Cache-Control", "no-cache")
            .header("Pragma", "no-cache")
            .header("DNT", "1")
            .header("Cookie", &sess.c)
            .header("X-XSRF-TOKEN", &xsrf)
            .body(body)
            .send()
            .await
            .map_err(|e| format!("tempmailg get_messages: {}", e))?;
        if !resp.status().is_success() {
            return Err(format!("tempmailg get_messages: {}", resp.status()));
        }
        let raw = resp.text().await.map_err(|e| e.to_string())?;
        let wrap: serde_json::Value =
            serde_json::from_str(&raw).map_err(|e| format!("tempmailg json: {}", e))?;
        if wrap.get("status").and_then(|x| x.as_bool()) != Some(true) {
            return Err("tempmailg: get_messages failed".into());
        }
        if let Some(mb) = wrap.get("mailbox").and_then(|x| x.as_str()) {
            if !mb.trim().is_empty() && !mb.trim().eq_ignore_ascii_case(email.trim()) {
                return Err("tempmailg: mailbox mismatch".into());
            }
        }
        let list = wrap
            .get("messages")
            .and_then(|x| x.as_array())
            .cloned()
            .unwrap_or_default();
        let mut out = Vec::with_capacity(list.len());
        for item in list {
            out.push(normalize_email(&item, email));
        }
        Ok(out)
    })
}
