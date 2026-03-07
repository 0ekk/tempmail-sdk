/*!
 * tempmail.la 渠道实现（支持分页）
 */

use serde_json::Value;
use crate::types::{Channel, EmailInfo, Email};
use crate::normalize::normalize_email;
use crate::config::{http_client, block_on, get_current_ua};

const BASE_URL: &str = "https://tempmail.la/api";

pub fn generate_email() -> Result<EmailInfo, String> {
    block_on(async {
        let resp = http_client()
            .post(format!("{}/mail/create", BASE_URL))
            .header("Content-Type", "application/json")
            .header("User-Agent", get_current_ua())
            .header("dnt", "1")
            .header("locale", "zh-CN")
            .header("origin", "https://tempmail.la")
            .header("platform", "PC")
            .header("product", "TEMP_MAIL")
            .header("referer", "https://tempmail.la/zh-CN/tempmail")
            .json(&serde_json::json!({"turnstile": ""}))
            .send().await.map_err(|e| format!("tempmail-la request failed: {}", e))?;

        if !resp.status().is_success() {
            return Err(format!("tempmail-la generate failed: {}", resp.status()));
        }

        let data: Value = resp.json().await.map_err(|e| format!("parse failed: {}", e))?;
        if data["code"].as_i64() != Some(0) {
            return Err("Failed to generate email".into());
        }

        Ok(EmailInfo {
            channel: Channel::TempmailLa,
            email: data["data"]["address"].as_str().unwrap_or("").to_string(),
            token: None,
            expires_at: data["data"]["endAt"].as_i64(),
            created_at: data["data"]["startAt"].as_str().map(|s| s.to_string()),
        })
    })
}

pub fn get_emails(email: &str) -> Result<Vec<Email>, String> {
    let email = email.to_string();
    block_on(async {
        let mut all_emails = Vec::new();
        let mut cursor: Option<String> = None;

        loop {
            let resp = http_client()
                .post(format!("{}/mail/box", BASE_URL))
                .header("Content-Type", "application/json")
                .header("User-Agent", get_current_ua())
                .header("dnt", "1")
                .header("origin", "https://tempmail.la")
                .header("platform", "PC")
                .header("product", "TEMP_MAIL")
                .header("referer", "https://tempmail.la/zh-CN/tempmail")
                .json(&serde_json::json!({"address": &email, "cursor": cursor}))
                .send().await.map_err(|e| format!("tempmail-la request failed: {}", e))?;

            if !resp.status().is_success() {
                return Err(format!("tempmail-la get emails failed: {}", resp.status()));
            }

            let data: Value = resp.json().await.map_err(|e| format!("parse failed: {}", e))?;
            if data["code"].as_i64() != Some(0) {
                return Err("Failed to get emails".into());
            }

            if let Some(rows) = data["data"]["rows"].as_array() {
                for raw in rows {
                    all_emails.push(normalize_email(raw, &email));
                }
            }

            if data["data"]["hasMore"].as_bool() == Some(true) {
                if let Some(c) = data["data"]["cursor"].as_str() {
                    cursor = Some(c.to_string());
                    continue;
                }
            }
            break;
        }

        Ok(all_emails)
    })
}
