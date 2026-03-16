import { InternalEmailInfo, Email, Channel } from '../types';
import { normalizeEmail } from '../normalize';
import { fetchWithTimeout } from '../retry';

const CHANNEL: Channel = 'chatgpt-org-uk';
const BASE_URL = 'https://mail.chatgpt.org.uk/api';
const HOME_URL = 'https://mail.chatgpt.org.uk/';

const DEFAULT_HEADERS = {
  'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36',
  'Accept': '*/*',
  'Referer': 'https://mail.chatgpt.org.uk/',
  'Origin': 'https://mail.chatgpt.org.uk',
  'DNT': '1',
};

function extractGmSid(response: Response): string {
  const setCookie = response.headers.get('set-cookie') || '';
  const match = setCookie.match(/gm_sid=([^;]+)/);
  return match ? match[1] : '';
}

async function fetchGmSid(): Promise<string> {
  const response = await fetchWithTimeout(HOME_URL, {
    method: 'GET',
    headers: DEFAULT_HEADERS,
  });

  if (!response.ok) {
    throw new Error(`Failed to fetch gm_sid: ${response.status}`);
  }

  const gmSid = extractGmSid(response);
  if (!gmSid) {
    throw new Error('Failed to extract gm_sid cookie');
  }

  return gmSid;
}

async function fetchGmSidWithRetry(): Promise<string> {
  try {
    return await fetchGmSid();
  } catch (error: any) {
    const message = String(error?.message || error || '').toLowerCase();
    if (message.includes('401') || message.includes('extract gm_sid')) {
      return await fetchGmSid();
    }
    throw error;
  }
}

async function fetchInboxToken(email: string, gmSid: string): Promise<string> {
  const response = await fetchWithTimeout(`${BASE_URL}/inbox-token`, {
    method: 'POST',
    headers: {
      ...DEFAULT_HEADERS,
      'Content-Type': 'application/json',
      'Cookie': `gm_sid=${gmSid}`,
    },
    body: JSON.stringify({ email }),
  });

  if (!response.ok) {
    throw new Error(`Failed to get inbox token: ${response.status}`);
  }

  const data = await response.json();
  const token = data?.auth?.token;
  if (!token) {
    throw new Error('Failed to get inbox token');
  }

  return token;
}

async function fetchInboxTokenWithRetry(email: string): Promise<string> {
  const gmSid = await fetchGmSidWithRetry();
  try {
    return await fetchInboxToken(email, gmSid);
  } catch (error: any) {
    const message = String(error?.message || error || '').toLowerCase();
    if (message.includes('401')) {
      const refreshedGmSid = await fetchGmSidWithRetry();
      return await fetchInboxToken(email, refreshedGmSid);
    }
    throw error;
  }
}

async function fetchEmails(token: string, email: string): Promise<Email[]> {
  if (!token) {
    throw new Error('internal error: token missing for chatgpt-org-uk');
  }
  const encodedEmail = encodeURIComponent(email);
  const response = await fetchWithTimeout(`${BASE_URL}/emails?email=${encodedEmail}`, {
    method: 'GET',
    headers: {
      ...DEFAULT_HEADERS,
      'x-inbox-token': token,
    },
  });

  if (!response.ok) {
    throw new Error(`Failed to get emails: ${response.status}`);
  }

  const data = await response.json();

  if (!data.success) {
    throw new Error('Failed to get emails');
  }

  const rawEmails = data.data?.emails || [];
  return rawEmails.map((raw: any) => normalizeEmail(raw, email));
}

async function fetchEmailsWithRetry(email: string): Promise<Email[]> {
  const token = await fetchInboxTokenWithRetry(email);
  try {
    return await fetchEmails(token, email);
  } catch (error: any) {
    const message = String(error?.message || error || '').toLowerCase();
    if (message.includes('401')) {
      const refreshedToken = await fetchInboxTokenWithRetry(email);
      return await fetchEmails(refreshedToken, email);
    }
    throw error;
  }
}

export async function generateEmail(): Promise<InternalEmailInfo> {
  const response = await fetchWithTimeout(`${BASE_URL}/generate-email`, {
    method: 'GET',
    headers: DEFAULT_HEADERS,
  });

  if (!response.ok) {
    throw new Error(`Failed to generate email: ${response.status}`);
  }

  const data = await response.json();

  if (!data.success) {
    throw new Error('Failed to generate email');
  }

  const email = data.data.email;
  const token = await fetchInboxTokenWithRetry(email);

  return {
    channel: CHANNEL,
    email,
    token,
  };
}

export async function getEmails(token: string, email: string): Promise<Email[]> {
  if (!token) {
    throw new Error('internal error: token missing for chatgpt-org-uk');
  }

  try {
    return await fetchEmails(token, email);
  } catch (error: any) {
    const message = String(error?.message || error || '').toLowerCase();
    if (message.includes('401')) {
      return await fetchEmailsWithRetry(email);
    }
    throw error;
  }
}
