import { ANTHROPIC_KEY } from '../config.js';

export async function claudeCall(prompt, maxTokens) {
  const res = await fetch('https://api.anthropic.com/v1/messages', {
    method: 'POST',
    headers: {
      'x-api-key':                               ANTHROPIC_KEY,
      'anthropic-version':                       '2023-06-01',
      'anthropic-dangerous-direct-browser-access': 'true',
      'content-type':                            'application/json',
    },
    body: JSON.stringify({
      model:      'claude-haiku-4-5-20251001',
      max_tokens: maxTokens,
      messages:   [{ role: 'user', content: prompt }],
    }),
  });
  if (!res.ok) throw new Error('Claude API error: ' + await res.text());
  return res.json();
}

export function safeParseJSON(text, fallback) {
  // 1. strip code fences and try direct parse
  try {
    const clean = text.replace(/^```[^\n]*\n?/, '').replace(/\n?```$/, '').trim();
    return JSON.parse(clean);
  } catch {}

  // 2. pull the first [...] block out of whatever else Haiku added
  try {
    const match = text.match(/\[[\s\S]*\]/);
    if (match) return JSON.parse(match[0]);
  } catch {}

  return fallback;
}
