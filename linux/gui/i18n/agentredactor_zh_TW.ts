<?xml version='1.0' encoding='utf-8'?>
<TS version="2.1" language="zh_TW">
  <context>
    <name>MainWindow</name>
    <message>
      <source>Agent Redactor</source>
      <translation>Agent Redactor</translation>
    </message>
    <message>
      <source>Update ready to install</source>
      <translation>更新已準備好安裝</translation>
    </message>
    <message>
      <source>Agent Redactor %1 has been downloaded. Restart now to apply the update.</source>
      <translation>Agent Redactor %1 已下載。立即重新啟動以套用更新。</translation>
    </message>
    <message>
      <source>Restart now</source>
      <translation>立即重新啟動</translation>
    </message>
    <message>
      <source>Later</source>
      <translation>稍後</translation>
    </message>
    <message>
      <source>Check for updates</source>
      <translation>檢查更新</translation>
    </message>
    <message>
      <source>You're up to date.</source>
      <translation>您已是最新版本。</translation>
    </message>
    <message>
      <source>Couldn't check for updates. Try again later.</source>
      <translation>無法檢查更新。請稍後重試。</translation>
    </message>
    <message>
      <source>On</source>
      <translation>在</translation>
    </message>
    <message>
      <source>Off</source>
      <translation>離開</translation>
    </message>
    <message>
      <source>How to use Agent Redactor</source>
      <translation>如何使用 Agent Redactor</translation>
    </message>
    <message>
      <source>API Proxy</source>
      <translation>API 代理</translation>
    </message>
    <message>
      <source>AI Powered Detection Model</source>
      <translation>AI 驅動偵測模型</translation>
    </message>
    <message>
      <source>Regex Patterns</source>
      <translation>規則運算式模式</translation>
    </message>
    <message>
      <source>Keywords</source>
      <translation>關鍵字</translation>
    </message>
    <message>
      <source>Password</source>
      <translation>密碼</translation>
    </message>
    <message>
      <source>Statistics</source>
      <translation>統計資料</translation>
    </message>
    <message>
      <source>Session Redactions</source>
      <translation>工作階段去識別化</translation>
    </message>
    <message>
      <source>Logs</source>
      <translation>記錄</translation>
    </message>
    <message>
      <source>Settings</source>
      <translation>設定</translation>
    </message>
    <message>
      <source>Name:</source>
      <translation>姓名：</translation>
    </message>
    <message>
      <source>Local URL</source>
      <translation>本機 URL</translation>
    </message>
    <message>
      <source>Forward To</source>
      <translation>轉送至</translation>
    </message>
    <message>
      <source>API Key</source>
      <translation>API 金鑰</translation>
    </message>
    <message>
      <source>Confidence threshold:</source>
      <translation>信賴閾值：</translation>
    </message>
    <message>
      <source>Profiles</source>
      <translation>設定檔</translation>
    </message>
    <message>
      <source>1. Configure your profile below (or use the default)</source>
      <translation>1. 在下方設定您的設定檔（或使用預設設定）</translation>
    </message>
    <message>
      <source>2. Point your LLM client (Claude Code, OpenClaw, etc.) at the Local URL shown below</source>
      <translation>2. 將您的 LLM 用戶端（Claude Code、OpenClaw 等）指向下方顯示的本機 URL</translation>
    </message>
    <message>
      <source>3. We sit between your client and real API. Everything stays on your machine. Sensitive data is redacted locally before any request leaves your computer ensuring your data never touches our server</source>
      <translation>3. 我們位於您的用戶端與真實 API 之間。所有內容都保留在您的裝置上。敏感資料會在任何要求離開您的電腦之前於本機去識別化，確保您的資料絕不會到達我們的伺服器</translation>
    </message>
    <message>
      <source>The address your LLM client points at</source>
      <translation>您的 LLM 用戶端指向的位址</translation>
    </message>
    <message>
      <source>The real API endpoint that receives your redacted requests</source>
      <translation>接收您去識別化後要求的真實 API 端點</translation>
    </message>
    <message>
      <source>AI-powered detection runs locally as an additional layer of defense. May miss data or over-redact. Use Regex Patterns and Keywords below for deterministic redaction.</source>
      <translation>AI 驅動偵測在本機執行，作為額外防禦層。可能會遺漏資料或過度去識別化。請使用下方的規則運算式模式和關鍵字進行確定性去識別化。</translation>
    </message>
    <message>
      <source>Expect slightly slower responses when enabled. The model scans every message locally.</source>
      <translation>啟用後回應速度可能會稍慢。模型會在本機掃描每則訊息。</translation>
    </message>
    <message>
      <source>Text matching these patterns will be redacted before sending to the API</source>
      <translation>與這些模式相符的文字將在傳送至 API 之前去識別化</translation>
    </message>
    <message>
      <source>Messages containing these words will be flagged for redaction</source>
      <translation>包含這些詞彙的訊息將被標記為去識別化</translation>
    </message>
    <message>
      <source>Actual redactions detected in the current session.</source>
      <translation>目前工作階段中偵測到的實際去識別化。</translation>
    </message>
    <message>
      <source>No redactions in current session.</source>
      <translation>目前工作階段中無去識別化。</translation>
    </message>
    <message>
      <source>Logs are stored on this PC. Redacted logs may still contain sensitive data that detection missed.</source>
      <translation>日誌儲存在此 PC 上。編輯後的日誌可能仍包含檢測遺漏的敏感資料。</translation>
    </message>
    <message>
      <source>e.g., Work OpenAI</source>
      <translation>例如，工作 OpenAI</translation>
    </message>
    <message>
      <source>e.g. https://openrouter.ai/api/v1</source>
      <translation>例如 https://openrouter.ai/api/v1</translation>
    </message>
    <message>
      <source>e.g. sk-[a-zA-Z0-9]{20,}</source>
      <translation>例如 sk-[a-zA-Z0-9]{20,}</translation>
    </message>
    <message>
      <source>e.g. password</source>
      <translation>例如 password</translation>
    </message>
    <message>
      <source>Add</source>
      <translation>新增</translation>
    </message>
    <message>
      <source>Remove</source>
      <translation>移除</translation>
    </message>
    <message>
      <source>Show API key</source>
      <translation>顯示 API 金鑰</translation>
    </message>
    <message>
      <source>Copy</source>
      <translation>複製</translation>
    </message>
    <message>
      <source>Save</source>
      <translation>節省</translation>
    </message>
    <message>
      <source>Use AI model for PII detection</source>
      <translation>使用AI模型進行PII檢測</translation>
    </message>
    <message>
      <source>Case sensitive</source>
      <translation>區分大小寫</translation>
    </message>
    <message>
      <source>Require master password</source>
      <translation>需要主密碼</translation>
    </message>
    <message>
      <source>Clear statistics</source>
      <translation>清晰的統計數據</translation>
    </message>
    <message>
      <source>Clear</source>
      <translation>清除</translation>
    </message>
    <message>
      <source>Enable logging</source>
      <translation>啟用日誌記錄</translation>
    </message>
    <message>
      <source>Show sensitive information in logs</source>
      <translation>在日誌中顯示敏感資訊</translation>
    </message>
    <message>
      <source>Open log file</source>
      <translation>開啟記錄檔</translation>
    </message>
    <message>
      <source>Open folder</source>
      <translation>開啟資料夾</translation>
    </message>
    <message>
      <source>Delete all logs</source>
      <translation>刪除所有記錄</translation>
    </message>
    <message>
      <source>Start on Boot</source>
      <translation>開機啟動</translation>
    </message>
    <message>
      <source>Language</source>
      <translation>語言</translation>
    </message>
    <message>
      <source>System default</source>
      <translation>系統預設</translation>
    </message>
    <message>
      <source>Master Password</source>
      <translation>主密碼</translation>
    </message>
    <message>
      <source>Unlock</source>
      <translation>解除鎖定</translation>
    </message>
    <message>
      <source>Agent Redactor is locked</source>
      <translation>代理編輯器已鎖定</translation>
    </message>
    <message>
      <source>Profile name</source>
      <translation>個人資料名稱</translation>
    </message>
    <message>
      <source>Proxy port</source>
      <translation>代理端口</translation>
    </message>
    <message>
      <source>Forward To URL</source>
      <translation>轉寄到 URL</translation>
    </message>
    <message>
      <source>API key</source>
      <translation>API金鑰</translation>
    </message>
    <message>
      <source>Confidence threshold</source>
      <translation>置信閾值</translation>
    </message>
    <message>
      <source>New regex pattern</source>
      <translation>新的正規表示式模式</translation>
    </message>
    <message>
      <source>New keyword</source>
      <translation>新關鍵字</translation>
    </message>
    <message>
      <source>Session redactions</source>
      <translation>會話修訂</translation>
    </message>
    <message>
      <source>Master password</source>
      <translation>主密碼</translation>
    </message>
    <message>
      <source>Enabled</source>
      <translation>已啟用</translation>
    </message>
    <message>
      <source>Regex Pattern</source>
      <translation>規則運算式模式</translation>
    </message>
    <message>
      <source>Case</source>
      <translation>大小寫</translation>
    </message>
    <message>
      <source>Keyword</source>
      <translation>關鍵字</translation>
    </message>
    <message>
      <source>Account number</source>
      <translation>帳號</translation>
    </message>
    <message>
      <source>Address</source>
      <translation>地址</translation>
    </message>
    <message>
      <source>Date</source>
      <translation>日期</translation>
    </message>
    <message>
      <source>Email</source>
      <translation>電子郵件</translation>
    </message>
    <message>
      <source>Person</source>
      <translation>人物</translation>
    </message>
    <message>
      <source>Phone</source>
      <translation>電話</translation>
    </message>
    <message>
      <source>URL</source>
      <translation>URL</translation>
    </message>
    <message>
      <source>Secret</source>
      <translation>機密</translation>
    </message>
    <message>
      <source>Requests: %1   PII: %2   Regex: %3   Keywords: %4</source>
      <translation>請求：%1 PII：%2 正規表示式：%3 關鍵字：%4</translation>
    </message>
    <message>
      <source>Engine is not running — retrying…</source>
      <translation>引擎未運作 — 正在重試...</translation>
    </message>
    <message>
      <source>Default</source>
      <translation>預設</translation>
    </message>
    <message>
      <source>Enable pattern</source>
      <translation>啟用模式</translation>
    </message>
    <message>
      <source>Regex pattern</source>
      <translation>正規表示式模式</translation>
    </message>
    <message>
      <source>Delete</source>
      <translation>刪除</translation>
    </message>
    <message>
      <source>Validation Error</source>
      <translation>驗證錯誤</translation>
    </message>
    <message>
      <source>Invalid regex syntax.</source>
      <translation>規則運算式語法無效。</translation>
    </message>
    <message>
      <source>Enable keyword</source>
      <translation>啟用關鍵字</translation>
    </message>
    <message>
      <source>Yes</source>
      <translation>是</translation>
    </message>
    <message>
      <source>No</source>
      <translation>否</translation>
    </message>
    <message>
      <source>Keyword text</source>
      <translation>關鍵字文字</translation>
    </message>
    <message>
      <source>Port must be between 1024 and 65535.</source>
      <translation>連接埠必須介於 1024 與 65535 之間。</translation>
    </message>
    <message>
      <source>Port %1 is already used by profile '%2'.</source>
      <translation>連接埠 %1 已被設定檔「%2」使用。</translation>
    </message>
    <message>
      <source>Forward To URL must start with http:// or https://.</source>
      <translation>轉送 URL 必須以 http:// 或 https:// 開頭。</translation>
    </message>
    <message>
      <source>Confidence threshold must be between 0.0 and 1.0.</source>
      <translation>信賴度閾值必須介於 0.0 與 1.0 之間。</translation>
    </message>
    <message>
      <source>Security Warning</source>
      <translation>安全性警告</translation>
    </message>
    <message>
      <source>You are using an HTTP (unencrypted) upstream URL. Your API key will be sent in plaintext over the network.</source>
      <translation>您正在使用 HTTP（未加密）上游 URL。您的 API 金鑰將以明文形式透過網路傳送。</translation>
    </message>
    <message>
      <source>Error</source>
      <translation>錯誤</translation>
    </message>
    <message>
      <source>The engine rejected the profile. Check the engine log for details.</source>
      <translation>引擎拒絕了該設定檔。檢查引擎日誌以了解詳細資訊。</translation>
    </message>
    <message>
      <source>Profile %1</source>
      <translation>設定檔%1</translation>
    </message>
    <message>
      <source>The engine rejected the new profile.</source>
      <translation>引擎拒絕了新的設定檔。</translation>
    </message>
    <message>
      <source>Remove Profile</source>
      <translation>移除設定檔</translation>
    </message>
    <message>
      <source>Are you sure? This operation is permanent.</source>
      <translation>是否確定？此操作無法復原。</translation>
    </message>
    <message>
      <source>Proxy URL copied to clipboard</source>
      <translation>代理 URL 已複製到剪貼簿</translation>
    </message>
    <message>
      <source>Port %1 is available</source>
      <translation>連接埠 %1 可用</translation>
    </message>
    <message>
      <source>Port %1 is already in use</source>
      <translation>連接埠 %1 已被使用</translation>
    </message>
    <message>
      <source>Wrong password.</source>
      <translation>密碼錯誤。</translation>
    </message>
    <message>
      <source>Show sensitive information</source>
      <translation>顯示敏感訊息</translation>
    </message>
    <message>
      <source>Sensitive logging writes raw, unredacted values (including API keys) to the log. Only enable it while debugging.</source>
      <translation>敏感日志记录将原始的、未编辑的值（包括 API 密钥）写入日志。僅在調試時啟用它。</translation>
    </message>
    <message>
      <source>Enable logging first.</source>
      <translation>首先啟用日誌記錄。</translation>
    </message>
    <message>
      <source>Delete all logs?</source>
      <translation>刪除所有記錄？</translation>
    </message>
    <message>
      <source>This will permanently delete the current log file and all archived session logs. This cannot be undone.</source>
      <translation>這將永久刪除目前記錄檔和所有已封存的作業記錄。無法復原。</translation>
    </message>
    <message>
      <source>Downloading AI model</source>
      <translation>正在下載 AI 模型</translation>
    </message>
    <message>
      <source>Retry</source>
      <translation>重試</translation>
    </message>
    <message>
      <source>The PII detection model is downloading (%1%).</source>
      <translation>正在下載 PII 檢測模型 (%1%)。</translation>
    </message>
    <message>
      <source>The model download failed. Check your internet connection, then retry. PII detection is unavailable until the download completes.</source>
      <translation>模型下載失敗。請檢查網路連線，然後重試。下載完成前，PII 偵測無法使用。</translation>
    </message>
    <message>
      <source>Are you sure you want to quit?</source>
      <translation>是否確定要結束？</translation>
    </message>
    <message>
      <source>If you quit, Agent Redactor will no longer monitor and redact API traffic.</source>
      <translation>如果結束，Agent Redactor 將不再監控和去識別化 API 流量。</translation>
    </message>
    <message>
      <source>Quit Agent Redactor? The engine keeps running in the background.</source>
      <translation>退出 Agent Redactor？引擎在後台持續運轉。</translation>
    </message>
  </context>
  <context>
    <name>TrayIcon</name>
    <message>
      <source>Agent Redactor</source>
      <translation>Agent Redactor</translation>
    </message>
    <message>
      <source>Open Agent Redactor</source>
      <translation>開啟 Agent Redactor</translation>
    </message>
    <message>
      <source>Start on Boot</source>
      <translation>開機啟動</translation>
    </message>
    <message>
      <source>Language</source>
      <translation>語言</translation>
    </message>
    <message>
      <source>Quit</source>
      <translation>結束</translation>
    </message>
  </context>
  <context>
    <name>PasswordEnableDialog</name>
    <message>
      <source>Enable password protection</source>
      <translation>啟用密碼保護</translation>
    </message>
    <message>
      <source>Choose a master password for Agent Redactor. It protects your stored API keys on this machine and is unrelated to your login password.</source>
      <translation>選擇 Agent Redactor 的主密碼。它保護您在本機上儲存的 API 金鑰，與您的登入密碼無關。</translation>
    </message>
    <message>
      <source>New password:</source>
      <translation>新密碼：</translation>
    </message>
    <message>
      <source>Confirm password:</source>
      <translation>確認密碼：</translation>
    </message>
    <message>
      <source>Password must not be empty.</source>
      <translation>密碼不能為空。</translation>
    </message>
    <message>
      <source>Passwords do not match.</source>
      <translation>密碼不符。</translation>
    </message>
  </context>
  <context>
    <name>PasswordUnlockDialog</name>
    <message>
      <source>Unlock Agent Redactor</source>
      <translation>解鎖代理編輯器</translation>
    </message>
    <message>
      <source>Enter your master password to unlock.</source>
      <translation>輸入您的主密碼進行解鎖。</translation>
    </message>
  </context>
</TS>