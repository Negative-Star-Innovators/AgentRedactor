<?xml version='1.0' encoding='utf-8'?>
<TS version="2.1" language="ja">
  <context>
    <name>MainWindow</name>
    <message>
      <source>Agent Redactor</source>
      <translation>Agent Redactor</translation>
    </message>
    <message>
      <source>Update ready to install</source>
      <translation>更新をインストールできます</translation>
    </message>
    <message>
      <source>Agent Redactor %1 has been downloaded. Restart now to apply the update.</source>
      <translation>Agent Redactor %1 がダウンロードされました。今すぐ再起動して更新を適用してください。</translation>
    </message>
    <message>
      <source>Restart now</source>
      <translation>今すぐ再起動</translation>
    </message>
    <message>
      <source>Later</source>
      <translation>後で</translation>
    </message>
    <message>
      <source>Check for updates</source>
      <translation>更新を確認</translation>
    </message>
    <message>
      <source>You're up to date.</source>
      <translation>最新の状態です。</translation>
    </message>
    <message>
      <source>Couldn't check for updates. Try again later.</source>
      <translation>更新を確認できませんでした。後でもう一度お試しください。</translation>
    </message>
    <message>
      <source>On</source>
      <translation>の上</translation>
    </message>
    <message>
      <source>Off</source>
      <translation>オフ</translation>
    </message>
    <message>
      <source>How to use Agent Redactor</source>
      <translation>Agent Redactor の使い方</translation>
    </message>
    <message>
      <source>API Proxy</source>
      <translation>API プロキシ</translation>
    </message>
    <message>
      <source>AI Powered Detection Model</source>
      <translation>AI 駆動検出モデル</translation>
    </message>
    <message>
      <source>Regex Patterns</source>
      <translation>正規表現パターン</translation>
    </message>
    <message>
      <source>Keywords</source>
      <translation>キーワード</translation>
    </message>
    <message>
      <source>Password</source>
      <translation>パスワード</translation>
    </message>
    <message>
      <source>Statistics</source>
      <translation>統計</translation>
    </message>
    <message>
      <source>Session Redactions</source>
      <translation>セッション編集</translation>
    </message>
    <message>
      <source>Logs</source>
      <translation>ログ</translation>
    </message>
    <message>
      <source>Settings</source>
      <translation>設定</translation>
    </message>
    <message>
      <source>Name:</source>
      <translation>名前：</translation>
    </message>
    <message>
      <source>Local URL</source>
      <translation>ローカル URL</translation>
    </message>
    <message>
      <source>Forward To</source>
      <translation>転送先</translation>
    </message>
    <message>
      <source>API Key</source>
      <translation>API キー</translation>
    </message>
    <message>
      <source>Confidence threshold:</source>
      <translation>信頼度のしきい値:</translation>
    </message>
    <message>
      <source>Profiles</source>
      <translation>プロファイル</translation>
    </message>
    <message>
      <source>1. Configure your profile below (or use the default)</source>
      <translation>1. 以下でプロファイルを構成してください（またはデフォルトを使用）</translation>
    </message>
    <message>
      <source>2. Point your LLM client (Claude Code, OpenClaw, etc.) at the Local URL shown below</source>
      <translation>2. LLM クライアント（Claude Code、OpenClaw など）を以下に表示されているローカル URL に向けてください</translation>
    </message>
    <message>
      <source>3. We sit between your client and real API. Everything stays on your machine. Sensitive data is redacted locally before any request leaves your computer ensuring your data never touches our server</source>
      <translation>3. クライアントと実際の API の間に位置します。すべてはお使いのマシン内に留まります。機密データは、リクエストがコンピュータから送信される前にローカルで編集され、データが当社のサーバーに触れることはありません</translation>
    </message>
    <message>
      <source>The address your LLM client points at</source>
      <translation>LLM クライアントが指し示すアドレス</translation>
    </message>
    <message>
      <source>The real API endpoint that receives your redacted requests</source>
      <translation>編集済みリクエストを受信する実際の API エンドポイント</translation>
    </message>
    <message>
      <source>AI-powered detection runs locally as an additional layer of defense. May miss data or over-redact. Use Regex Patterns and Keywords below for deterministic redaction.</source>
      <translation>AI 駆動検出は、追加の防御層としてローカルで実行されます。データを見逃したり、過度に編集したりする可能性があります。確定的な編集には、以下の正規表現パターンとキーワードを使用してください。</translation>
    </message>
    <message>
      <source>Expect slightly slower responses when enabled. The model scans every message locally.</source>
      <translation>有効にすると、応答がわずかに遅くなる場合があります。モデルはすべてのメッセージをローカルでスキャンします。</translation>
    </message>
    <message>
      <source>Text matching these patterns will be redacted before sending to the API</source>
      <translation>これらのパターンに一致するテキストは、API に送信される前に編集されます</translation>
    </message>
    <message>
      <source>Messages containing these words will be flagged for redaction</source>
      <translation>これらの単語を含むメッセージは編集対象としてフラグが立てられます</translation>
    </message>
    <message>
      <source>Actual redactions detected in the current session.</source>
      <translation>現在のセッションで検出された実際の編集。</translation>
    </message>
    <message>
      <source>No redactions in current session.</source>
      <translation>現在のセッションに編集はありません。</translation>
    </message>
    <message>
      <source>Logs are stored on this PC. Redacted logs may still contain sensitive data that detection missed.</source>
      <translation>ログはこの PC に保存されます。編集されたログには、検出できなかった機密データが含まれている可能性があります。</translation>
    </message>
    <message>
      <source>e.g., Work OpenAI</source>
      <translation>例: 仕事用 OpenAI</translation>
    </message>
    <message>
      <source>e.g. https://openrouter.ai/api/v1</source>
      <translation>例: https://openrouter.ai/api/v1</translation>
    </message>
    <message>
      <source>e.g. sk-[a-zA-Z0-9]{20,}</source>
      <translation>例: sk-[a-zA-Z0-9]{20,}</translation>
    </message>
    <message>
      <source>e.g. password</source>
      <translation>例: password</translation>
    </message>
    <message>
      <source>Add</source>
      <translation>追加</translation>
    </message>
    <message>
      <source>Remove</source>
      <translation>削除</translation>
    </message>
    <message>
      <source>Show API key</source>
      <translation>APIキーを表示</translation>
    </message>
    <message>
      <source>Copy</source>
      <translation>コピー</translation>
    </message>
    <message>
      <source>Save</source>
      <translation>保存</translation>
    </message>
    <message>
      <source>Use AI model for PII detection</source>
      <translation>PII 検出に AI モデルを使用する</translation>
    </message>
    <message>
      <source>Case sensitive</source>
      <translation>大文字小文字を区別</translation>
    </message>
    <message>
      <source>Require master password</source>
      <translation>マスターパスワードを要求する</translation>
    </message>
    <message>
      <source>Clear statistics</source>
      <translation>統計をクリアする</translation>
    </message>
    <message>
      <source>Clear</source>
      <translation>クリア</translation>
    </message>
    <message>
      <source>Enable logging</source>
      <translation>ロギングを有効にする</translation>
    </message>
    <message>
      <source>Show sensitive information in logs</source>
      <translation>機密情報をログに表示する</translation>
    </message>
    <message>
      <source>Open log file</source>
      <translation>ログファイルを開く</translation>
    </message>
    <message>
      <source>Open folder</source>
      <translation>フォルダーを開く</translation>
    </message>
    <message>
      <source>Delete all logs</source>
      <translation>すべてのログを削除</translation>
    </message>
    <message>
      <source>Start on Boot</source>
      <translation>ブート時に開始</translation>
    </message>
    <message>
      <source>Language</source>
      <translation>言語</translation>
    </message>
    <message>
      <source>System default</source>
      <translation>システム既定</translation>
    </message>
    <message>
      <source>Master Password</source>
      <translation>マスター パスワード</translation>
    </message>
    <message>
      <source>Unlock</source>
      <translation>ロック解除</translation>
    </message>
    <message>
      <source>Agent Redactor is locked</source>
      <translation>エージェント リダクターはロックされています</translation>
    </message>
    <message>
      <source>Profile name</source>
      <translation>プロファイル名</translation>
    </message>
    <message>
      <source>Proxy port</source>
      <translation>プロキシポート</translation>
    </message>
    <message>
      <source>Forward To URL</source>
      <translation>URL に転送</translation>
    </message>
    <message>
      <source>API key</source>
      <translation>APIキー</translation>
    </message>
    <message>
      <source>Confidence threshold</source>
      <translation>信頼閾値</translation>
    </message>
    <message>
      <source>New regex pattern</source>
      <translation>新しい正規表現パターン</translation>
    </message>
    <message>
      <source>New keyword</source>
      <translation>新しいキーワード</translation>
    </message>
    <message>
      <source>Session redactions</source>
      <translation>セッションの編集</translation>
    </message>
    <message>
      <source>Master password</source>
      <translation>マスターパスワード</translation>
    </message>
    <message>
      <source>Enabled</source>
      <translation>有効</translation>
    </message>
    <message>
      <source>Regex Pattern</source>
      <translation>正規表現パターン</translation>
    </message>
    <message>
      <source>Case</source>
      <translation>大文字小文字</translation>
    </message>
    <message>
      <source>Keyword</source>
      <translation>キーワード</translation>
    </message>
    <message>
      <source>Account number</source>
      <translation>口座番号</translation>
    </message>
    <message>
      <source>Address</source>
      <translation>住所</translation>
    </message>
    <message>
      <source>Date</source>
      <translation>日付</translation>
    </message>
    <message>
      <source>Email</source>
      <translation>メール</translation>
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
      <translation>秘密</translation>
    </message>
    <message>
      <source>Requests: %1   PII: %2   Regex: %3   Keywords: %4</source>
      <translation>リクエスト: %1 PII: %2 正規表現: %3 キーワード: %4</translation>
    </message>
    <message>
      <source>Engine is not running — retrying…</source>
      <translation>エンジンが動作していません - 再試行しています…</translation>
    </message>
    <message>
      <source>Default</source>
      <translation>既定</translation>
    </message>
    <message>
      <source>Enable pattern</source>
      <translation>有効パターン</translation>
    </message>
    <message>
      <source>Regex pattern</source>
      <translation>正規表現パターン</translation>
    </message>
    <message>
      <source>Delete</source>
      <translation>消去</translation>
    </message>
    <message>
      <source>Validation Error</source>
      <translation>検証エラー</translation>
    </message>
    <message>
      <source>Invalid regex syntax.</source>
      <translation>正規表現の構文が無効です。</translation>
    </message>
    <message>
      <source>This entry already exists.</source>
      <translation>このエントリは既に存在します。</translation>
    </message>
    <message>
      <source>Enable keyword</source>
      <translation>キーワードを有効にする</translation>
    </message>
    <message>
      <source>Yes</source>
      <translation>はい</translation>
    </message>
    <message>
      <source>No</source>
      <translation>いいえ</translation>
    </message>
    <message>
      <source>Keyword text</source>
      <translation>キーワードテキスト</translation>
    </message>
    <message>
      <source>Port must be between 1024 and 65535.</source>
      <translation>ポートは 1024 から 65535 の間である必要があります。</translation>
    </message>
    <message>
      <source>Port %1 is already used by profile '%2'.</source>
      <translation>ポート %1 はプロファイル「%2」によって使用されています。</translation>
    </message>
    <message>
      <source>Forward To URL must start with http:// or https://.</source>
      <translation>転送先 URL は http:// または https:// で始まる必要があります。</translation>
    </message>
    <message>
      <source>Confidence threshold must be between 0.0 and 1.0.</source>
      <translation>信頼度しきい値は 0.0 から 1.0 の間である必要があります。</translation>
    </message>
    <message>
      <source>Security Warning</source>
      <translation>セキュリティ警告</translation>
    </message>
    <message>
      <source>You are using an HTTP (unencrypted) upstream URL. Your API key will be sent in plaintext over the network.</source>
      <translation>HTTP（暗号化されていない）アップストリーム URL を使用しています。API キーはネットワークを介して平文で送信されます。</translation>
    </message>
    <message>
      <source>Error</source>
      <translation>エラー</translation>
    </message>
    <message>
      <source>The engine rejected the profile. Check the engine log for details.</source>
      <translation>エンジンがプロファイルを拒否しました。詳細については、エンジン ログを確認してください。</translation>
    </message>
    <message>
      <source>Profile %1</source>
      <translation>プロファイル %1</translation>
    </message>
    <message>
      <source>The engine rejected the new profile.</source>
      <translation>エンジンは新しいプロファイルを拒否しました。</translation>
    </message>
    <message>
      <source>Remove Profile</source>
      <translation>プロファイルを削除</translation>
    </message>
    <message>
      <source>Are you sure? This operation is permanent.</source>
      <translation>よろしいですか？この操作は元に戻せません。</translation>
    </message>
    <message>
      <source>Proxy URL copied to clipboard</source>
      <translation>プロキシ URL がクリップボードにコピーされました</translation>
    </message>
    <message>
      <source>Port %1 is available</source>
      <translation>ポート %1 は使用可能です</translation>
    </message>
    <message>
      <source>Port %1 is already in use</source>
      <translation>ポート %1 は既に使用されています</translation>
    </message>
    <message>
      <source>Wrong password.</source>
      <translation>パスワードが間違っています。</translation>
    </message>
    <message>
      <source>Show sensitive information</source>
      <translation>機密情報を表示する</translation>
    </message>
    <message>
      <source>Sensitive logging writes raw, unredacted values (including API keys) to the log. Only enable it while debugging.</source>
      <translation>機密ログでは、編集されていない生の値 (API キーを含む) がログに書き込まれます。デバッグ中にのみ有効にしてください。</translation>
    </message>
    <message>
      <source>Enable logging first.</source>
      <translation>まずログを有効にします。</translation>
    </message>
    <message>
      <source>Delete all logs?</source>
      <translation>すべてのログを削除しますか？</translation>
    </message>
    <message>
      <source>This will permanently delete the current log file and all archived session logs. This cannot be undone.</source>
      <translation>現在のログファイルとすべてのアーカイブ済みセッション ログが完全に削除されます。この操作は元に戻せません。</translation>
    </message>
    <message>
      <source>Downloading AI model</source>
      <translation>AI モデルをダウンロードしています</translation>
    </message>
    <message>
      <source>Retry</source>
      <translation>再試行</translation>
    </message>
    <message>
      <source>The PII detection model is downloading (%1%).</source>
      <translation>PII 検出モデルをダウンロード中です (%1%)。</translation>
    </message>
    <message>
      <source>The model download failed. Check your internet connection, then retry. PII detection is unavailable until the download completes.</source>
      <translation>モデルのダウンロードに失敗しました。インターネット接続を確認してから再試行してください。ダウンロードが完了するまで PII 検出は利用できません。</translation>
    </message>
    <message>
      <source>Are you sure you want to quit?</source>
      <translation>終了してもよろしいですか？</translation>
    </message>
    <message>
      <source>If you quit, Agent Redactor will no longer monitor and redact API traffic.</source>
      <translation>終了すると、Agent Redactor は API トラフィックの監視と編集を行わなくなります。</translation>
    </message>
    <message>
      <source>Quit Agent Redactor? The engine keeps running in the background.</source>
      <translation>エージェント・リダクターを辞めますか?エンジンはバックグラウンドで動作し続けます。</translation>
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
      <translation>Agent Redactor を開く</translation>
    </message>
    <message>
      <source>Start on Boot</source>
      <translation>ブート時に開始</translation>
    </message>
    <message>
      <source>Language</source>
      <translation>言語</translation>
    </message>
    <message>
      <source>Quit</source>
      <translation>終了</translation>
    </message>
  </context>
  <context>
    <name>PasswordEnableDialog</name>
    <message>
      <source>Enable password protection</source>
      <translation>パスワード保護を有効にする</translation>
    </message>
    <message>
      <source>Choose a master password for Agent Redactor. It protects your stored API keys on this machine and is unrelated to your login password.</source>
      <translation>Agent Redactor のマスター パスワードを選択します。これは、このマシンに保存されている API キーを保護し、ログイン パスワードとは無関係です。</translation>
    </message>
    <message>
      <source>New password:</source>
      <translation>新しいパスワード：</translation>
    </message>
    <message>
      <source>Confirm password:</source>
      <translation>パスワードを認証する：</translation>
    </message>
    <message>
      <source>Password must not be empty.</source>
      <translation>パスワードを空にすることはできません。</translation>
    </message>
    <message>
      <source>Passwords do not match.</source>
      <translation>パスワードが一致しません。</translation>
    </message>
  </context>
  <context>
    <name>PasswordUnlockDialog</name>
    <message>
      <source>Unlock Agent Redactor</source>
      <translation>エージェント リダクターのロックを解除する</translation>
    </message>
    <message>
      <source>Enter your master password to unlock.</source>
      <translation>マスターパスワードを入力してロックを解除します。</translation>
    </message>
  </context>
</TS>