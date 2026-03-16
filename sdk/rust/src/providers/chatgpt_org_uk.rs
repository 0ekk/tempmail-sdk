/*!
 * mail.chatgpt.org.uk 渠道实现
 */

use serde_json::Value;
use crate::types::{Channel, EmailInfo, Email};
use crate::normalize::normalize_email;
use crate::config::{http_client, block_on, get_current_ua};

const BASE_URL: &str = "https://mail.chatgpt.org.uk/api";
const HOME_URL: &str = "https://mail.chatgpt.org.uk/";

fn is_401_error(message: &str) -> bool {
    message.contains("401")
}

fn is_gm_sid_error(message: &str) -> bool {
    message.to_lowercase().contains("gm_sid")
}

fn fetch_gm_sid_once() -> Result<String, String> {
    block_on(async {
        let resp = http_client()
            .get(HOME_URL)
            .header("User-Agent", get_current_ua())
            .header("Accept", "*/*")
            .header("Referer", HOME_URL)
            .header("Origin", "https://mail.chatgpt.org.uk")
            .header("DNT", "1")
            .send().await.map_err(|e| format!("chatgpt-org-uk home request failed: {}", e))?;

        if !resp.status().is_success() {
            return Err(format!("chatgpt-org-uk home failed: {}", resp.status()));
        }

        let gm_sid = resp.headers().get_all("set-cookie")
            .iter()
            .filter_map(|v| v.to_str().ok())
            .find_map(|s| {
                let part = s.split(';').next().unwrap_or("");
                if part.starts_with("gm_sid=") {
                    Some(part["gm_sid=".len()..].to_string())
                } else {
                    None
                }
            })
            .ok_or("Failed to extract gm_sid cookie")?;

        Ok(gm_sid)
    })
}

fn fetch_gm_sid() -> Result<String, String> {
    match fetch_gm_sid_once() {
        Ok(gm_sid) => Ok(gm_sid),
        Err(err) => {
            if is_401_error(&err) || is_gm_sid_error(&err) {
                return fetch_gm_sid_once();
            }
            Err(err)
        }
    }
}

fn fetch_inbox_token_once(email: &str, gm_sid: &str) -> Result<String, String> {
    let email = email.to_string();
    let gm_sid = gm_sid.to_string();
    block_on(async {
        let resp = http_client()
            .post(format!("{}/inbox-token", BASE_URL))
            .header("User-Agent", get_current_ua())
            .header("Accept", "*/*")
            .header("Referer", HOME_URL)
            .header("Origin", "https://mail.chatgpt.org.uk")
            .header("DNT", "1")
            .header("Content-Type", "application/json")
            .header("Cookie", format!("gm_sid={}", gm_sid))
            .json(&serde_json::json!({"email": email}))
            .send().await.map_err(|e| format!("chatgpt-org-uk inbox-token request failed: {}", e))?;

        if !resp.status().is_success() {
            return Err(format!("chatgpt-org-uk inbox-token failed: {}", resp.status()));
        }

        let data: Value = resp.json().await.map_err(|e| format!("parse failed: {}", e))?;
        let token = data["auth"]["token"].as_str().unwrap_or("");
        if token.is_empty() {
            return Err("Failed to get inbox token".into());
        }

        Ok(token.to_string())
    })
}

fn fetch_inbox_token(email: &str) -> Result<String, String> {
    let gm_sid = fetch_gm_sid()?;
    match fetch_inbox_token_once(email, &gm_sid) {
        Ok(token) => Ok(token),
        Err(err) => {
            if is_401_error(&err) {
                let gm_sid = fetch_gm_sid()?;
                return fetch_inbox_token_once(email, &gm_sid);
            }
            Err(err)
        }
    }
}

pub fn generate_email() -> Result<EmailInfo, String> {
    let email = block_on(async {
        let resp = http_client()
            .get(format!("{}/generate-email", BASE_URL))
            .header("User-Agent", get_current_ua())
            .header("Accept", "*/*")
            .header("Referer", HOME_URL)
            .header("Origin", "https://mail.chatgpt.org.uk")
            .header("DNT", "1")
            .send().await.map_err(|e| format!("chatgpt-org-uk request failed: {}", e))?;

        if !resp.status().is_success() {
            return Err(format!("chatgpt-org-uk generate failed: {}", resp.status()));
        }

        let data: Value = resp.json().await.map_err(|e| format!("parse failed: {}", e))?;
        if !data["success"].as_bool().unwrap_or(false) {
            return Err("Failed to generate email".into());
        }

        let email = data["data"]["email"].as_str().unwrap_or("").to_string();
        if email.is_empty() {
            return Err("Failed to generate email".into());
        }

        Ok(email)
    })?;

    let token = fetch_inbox_token(&email)?;

    Ok(EmailInfo {
        channel: Channel::ChatgptOrgUk,
        email,
        token: Some(token),
        expires_at: None,
        created_at: None,
    })
}

fn fetch_emails(token: &str, email: &str) -> Result<Vec<Email>, String> {
    let token = token.to_string();
    let email = email.to_string();
    block_on(async {
        let resp = http_client()
            .get(format!("{}/emails?email={}", BASE_URL, urlencoding::encode(&email)))
            .header("User-Agent", get_current_ua())
            .header("Accept", "*/*")
            .header("Referer", HOME_URL)
            .header("Origin", "https://mail.chatgpt.org.uk")
            .header("DNT", "1")
            .header("x-inbox-token", token)
            .send().await.map_err(|e| format!("chatgpt-org-uk request failed: {}", e))?;

        if !resp.status().is_success() {
            return Err(format!("chatgpt-org-uk get emails failed: {}", resp.status()));
        }

        let data: Value = resp.json().await.map_err(|e| format!("parse failed: {}", e))?;
        if !data["success"].as_bool().unwrap_or(false) {
            return Err("Failed to get emails".into());
        }

        Ok(data["data"]["emails"].as_array()
            .map(|arr| arr.iter().map(|raw| normalize_email(raw, &email)).collect())
            .unwrap_or_default())
    })
}

pub fn get_emails(token: &str, email: &str) -> Result<Vec<Email>, String> {
    if token.is_empty() {
        return Err("token is required for chatgpt-org-uk".into());
    }

    match fetch_emails(token, email) {
        Ok(emails) => Ok(emails),
        Err(err) => {
            if is_401_error(&err) {
                let refreshed = fetch_inbox_token(email)?;
                return fetch_emails(&refreshed, email);
            }
            Err(err)
        }
    }
}
