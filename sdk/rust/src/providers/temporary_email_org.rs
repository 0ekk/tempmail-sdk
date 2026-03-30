/*!
 * temporary-email.org 渠道
 * 首次 GET /zh/messages 下发 Cookie；收信需带 Cookie 与 X-Requested-With: XMLHttpRequest
 */

use serde_json::Value;
use crate::types::{Channel, EmailInfo, Email};
use crate::normalize::normalize_email;
use crate::config::{http_client, block_on, get_current_ua};

const MESSAGES_URL: &str = "https://www.temporary-email.org/zh/messages";
const REFERER: &str = "https://www.temporary-email.org/zh";

pub fn generate_email() -> Result<EmailInfo, String> {
    block_on(async {
        let resp = http_client()
            .get(MESSAGES_URL)
            .header("User-Agent", get_current_ua())
            .header("Accept", "text/plain, */*; q=0.01")
            .header("accept-language", "zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6")
            .header("cache-control", "no-cache")
            .header("dnt", "1")
            .header("pragma", "no-cache")
            .header("priority", "u=1, i")
            .header("referer", REFERER)
            .header("sec-ch-ua", r#""Chromium";v="146", "Not-A.Brand";v="24", "Microsoft Edge";v="146""#)
            .header("sec-ch-ua-mobile", "?0")
            .header("sec-ch-ua-platform", r#""Windows""#)
            .header("sec-fetch-dest", "empty")
            .header("sec-fetch-mode", "cors")
            .header("sec-fetch-site", "same-origin")
            .send().await.map_err(|e| format!("temporary-email-org request failed: {}", e))?;

        if !resp.status().is_success() {
            return Err(format!("temporary-email-org generate failed: {}", resp.status()));
        }

        let cookie: String = resp.headers().get_all("set-cookie")
            .iter()
            .filter_map(|v| v.to_str().ok())
            .filter_map(|s| s.split(';').next().map(|p| p.trim().to_string()))
            .filter(|p| !p.is_empty())
            .collect::<Vec<_>>()
            .join("; ");
        if cookie.is_empty() {
            return Err("temporary-email-org: no set-cookie".into());
        }
        if !cookie.contains("temporaryemail_session=") || !cookie.contains("email=") {
            return Err("temporary-email-org: missing session cookies".into());
        }

        let data: Value = resp.json().await.map_err(|e| format!("parse failed: {}", e))?;
        let mailbox = data["mailbox"].as_str().unwrap_or("").trim();
        if mailbox.is_empty() || !mailbox.contains('@') {
            return Err("temporary-email-org: invalid mailbox".into());
        }

        Ok(EmailInfo {
            channel: Channel::TemporaryEmailOrg,
            email: mailbox.to_string(),
            token: Some(cookie),
            expires_at: None,
            created_at: None,
        })
    })
}

pub fn get_emails(token: &str, email: &str) -> Result<Vec<Email>, String> {
    let token = token.to_string();
    let email = email.to_string();
    block_on(async {
        let resp = http_client()
            .get(MESSAGES_URL)
            .header("User-Agent", get_current_ua())
            .header("Accept", "text/plain, */*; q=0.01")
            .header("accept-language", "zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6")
            .header("cache-control", "no-cache")
            .header("dnt", "1")
            .header("pragma", "no-cache")
            .header("priority", "u=1, i")
            .header("referer", REFERER)
            .header("sec-ch-ua", r#""Chromium";v="146", "Not-A.Brand";v="24", "Microsoft Edge";v="146""#)
            .header("sec-ch-ua-mobile", "?0")
            .header("sec-ch-ua-platform", r#""Windows""#)
            .header("sec-fetch-dest", "empty")
            .header("sec-fetch-mode", "cors")
            .header("sec-fetch-site", "same-origin")
            .header("x-requested-with", "XMLHttpRequest")
            .header("Cookie", &token)
            .send().await.map_err(|e| format!("temporary-email-org request failed: {}", e))?;

        if !resp.status().is_success() {
            return Err(format!("temporary-email-org get emails failed: {}", resp.status()));
        }

        let data: Value = resp.json().await.map_err(|e| format!("parse failed: {}", e))?;
        let arr = data["messages"].as_array().cloned().unwrap_or_default();
        Ok(arr.iter().map(|raw| normalize_email(raw, &email)).collect())
    })
}
