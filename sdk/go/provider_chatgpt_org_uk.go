package tempemail

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/url"
	"strings"

	http "github.com/bogdanfinn/fhttp"
	tls_client "github.com/bogdanfinn/tls-client"
)

const chatgptOrgUkBaseURL = "https://mail.chatgpt.org.uk/api"
const chatgptOrgUkHomeURL = "https://mail.chatgpt.org.uk/"

var chatgptOrgUkHeaders = map[string]string{
	"Accept":  "*/*",
	"Referer": "https://mail.chatgpt.org.uk/",
	"Origin":  "https://mail.chatgpt.org.uk",
	"DNT":     "1",
}

func setChatgptOrgUkHeaders(req *http.Request) {
	for k, v := range chatgptOrgUkHeaders {
		req.Header.Set(k, v)
	}
	req.Header.Set("User-Agent", GetCurrentUA())
}

type chatgptOrgUkGenerateResponse struct {
	Success bool `json:"success"`
	Data    struct {
		Email string `json:"email"`
	} `json:"data"`
}

type chatgptOrgUkInboxTokenResponse struct {
	Auth struct {
		Token string `json:"token"`
	} `json:"auth"`
}

type chatgptOrgUkEmailsResponse struct {
	Success bool `json:"success"`
	Data    struct {
		Emails []json.RawMessage `json:"emails"`
		Count  int               `json:"count"`
	} `json:"data"`
}

func chatgptOrgUkFetchGmSid(client tls_client.HttpClient) (string, error) {
	req, err := http.NewRequest("GET", chatgptOrgUkHomeURL, nil)
	if err != nil {
		return "", err
	}
	setChatgptOrgUkHeaders(req)

	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	if err := checkHTTPStatus(resp, "chatgpt-org-uk home"); err != nil {
		return "", err
	}

	var gmSid string
	for _, cookie := range resp.Cookies() {
		if cookie.Name == "gm_sid" {
			gmSid = cookie.Value
			break
		}
	}
	if gmSid == "" {
		return "", fmt.Errorf("failed to extract gm_sid cookie")
	}
	return gmSid, nil
}

func chatgptOrgUkFetchGmSidWithRetry(client tls_client.HttpClient) (string, error) {
	gmSid, err := chatgptOrgUkFetchGmSid(client)
	if err == nil {
		return gmSid, nil
	}
	if strings.Contains(err.Error(), ": 401") || strings.Contains(err.Error(), "gm_sid") {
		return chatgptOrgUkFetchGmSid(client)
	}
	return "", err
}

func chatgptOrgUkFetchInboxToken(client tls_client.HttpClient, email string, gmSid string) (string, error) {
	payload, err := json.Marshal(map[string]string{"email": email})
	if err != nil {
		return "", err
	}

	req, err := http.NewRequest("POST", chatgptOrgUkBaseURL+"/inbox-token", bytes.NewReader(payload))
	if err != nil {
		return "", err
	}
	setChatgptOrgUkHeaders(req)
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Cookie", fmt.Sprintf("gm_sid=%s", gmSid))

	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	if err := checkHTTPStatus(resp, "chatgpt-org-uk inbox-token"); err != nil {
		return "", err
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", err
	}

	var result chatgptOrgUkInboxTokenResponse
	if err := json.Unmarshal(body, &result); err != nil {
		return "", err
	}
	if result.Auth.Token == "" {
		return "", fmt.Errorf("failed to get inbox token")
	}
	return result.Auth.Token, nil
}

func chatgptOrgUkFetchInboxTokenWithRetry(client tls_client.HttpClient, email string) (string, error) {
	gmSid, err := chatgptOrgUkFetchGmSidWithRetry(client)
	if err != nil {
		return "", err
	}

	token, err := chatgptOrgUkFetchInboxToken(client, email, gmSid)
	if err == nil {
		return token, nil
	}
	if strings.Contains(err.Error(), ": 401") {
		gmSid, err := chatgptOrgUkFetchGmSidWithRetry(client)
		if err != nil {
			return "", err
		}
		return chatgptOrgUkFetchInboxToken(client, email, gmSid)
	}
	return "", err
}

func chatgptOrgUkGenerate() (*EmailInfo, error) {
	client := HTTPClient()

	req, err := http.NewRequest("GET", chatgptOrgUkBaseURL+"/generate-email", nil)
	if err != nil {
		return nil, err
	}
	setChatgptOrgUkHeaders(req)

	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if err := checkHTTPStatus(resp, "chatgpt-org-uk generate"); err != nil {
		return nil, err
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}

	var result chatgptOrgUkGenerateResponse
	if err := json.Unmarshal(body, &result); err != nil {
		return nil, err
	}

	if !result.Success {
		return nil, fmt.Errorf("failed to generate email")
	}

	token, err := chatgptOrgUkFetchInboxTokenWithRetry(client, result.Data.Email)
	if err != nil {
		return nil, err
	}

	return &EmailInfo{
		Channel: ChannelChatgptOrgUk,
		Email:   result.Data.Email,
		token:   token,
	}, nil
}

func chatgptOrgUkGetEmails(email string, token string) ([]Email, error) {
	if token == "" {
		return nil, fmt.Errorf("missing inbox token")
	}
	encodedEmail := url.QueryEscape(email)

	fetchEmails := func(token string) ([]Email, error) {
		req, err := http.NewRequest("GET", chatgptOrgUkBaseURL+"/emails?email="+encodedEmail, nil)
		if err != nil {
			return nil, err
		}
		setChatgptOrgUkHeaders(req)
		req.Header.Set("x-inbox-token", token)

		client := HTTPClient()
		resp, err := client.Do(req)
		if err != nil {
			return nil, err
		}
		defer resp.Body.Close()

		if err := checkHTTPStatus(resp, "chatgpt-org-uk get emails"); err != nil {
			return nil, err
		}

		body, err := io.ReadAll(resp.Body)
		if err != nil {
			return nil, err
		}

		var result chatgptOrgUkEmailsResponse
		if err := json.Unmarshal(body, &result); err != nil {
			return nil, err
		}

		if !result.Success {
			return nil, fmt.Errorf("failed to get emails")
		}

		return normalizeRawEmails(result.Data.Emails, email)
	}

	emails, err := fetchEmails(token)
	if err == nil {
		return emails, nil
	}
	if strings.Contains(err.Error(), ": 401") {
		refreshedToken, refreshErr := chatgptOrgUkFetchInboxTokenWithRetry(HTTPClient(), email)
		if refreshErr != nil {
			return nil, err
		}
		return fetchEmails(refreshedToken)
	}
	return nil, err
}
