<?xml version='1.0' encoding='utf-8'?>
<TS version="2.1" language="cs">
  <context>
    <name>MainWindow</name>
    <message>
      <source>Agent Redactor</source>
      <translation>Agent Redactor</translation>
    </message>
    <message>
      <source>Update ready to install</source>
      <translation>Aktualizace připravena k instalaci</translation>
    </message>
    <message>
      <source>Agent Redactor %1 has been downloaded. Restart now to apply the update.</source>
      <translation>Agent Redactor %1 byl stažen. Restartujte nyní a aktualizaci použijte.</translation>
    </message>
    <message>
      <source>Restart now</source>
      <translation>Restartovat nyní</translation>
    </message>
    <message>
      <source>Later</source>
      <translation>Později</translation>
    </message>
    <message>
      <source>Check for updates</source>
      <translation>Zkontrolovat aktualizace</translation>
    </message>
    <message>
      <source>You're up to date.</source>
      <translation>Máte nejnovější verzi.</translation>
    </message>
    <message>
      <source>Couldn't check for updates. Try again later.</source>
      <translation>Nepodařilo se zkontrolovat aktualizace. Zkuste to znovu později.</translation>
    </message>
    <message>
      <source>On</source>
      <translation>Na</translation>
    </message>
    <message>
      <source>Off</source>
      <translation>Vypnuto</translation>
    </message>
    <message>
      <source>How to use Agent Redactor</source>
      <translation>Jak používat Agent Redactor</translation>
    </message>
    <message>
      <source>API Proxy</source>
      <translation>API proxy</translation>
    </message>
    <message>
      <source>AI Powered Detection Model</source>
      <translation>Model detekce poháněný umělou inteligencí</translation>
    </message>
    <message>
      <source>Regex Patterns</source>
      <translation>Regex vzory</translation>
    </message>
    <message>
      <source>Keywords</source>
      <translation>Klíčová slova</translation>
    </message>
    <message>
      <source>Password</source>
      <translation>Heslo</translation>
    </message>
    <message>
      <source>Statistics</source>
      <translation>Statistiky</translation>
    </message>
    <message>
      <source>Session Redactions</source>
      <translation>Redigace relace</translation>
    </message>
    <message>
      <source>Logs</source>
      <translation>Logy</translation>
    </message>
    <message>
      <source>Settings</source>
      <translation>Nastavení</translation>
    </message>
    <message>
      <source>Name:</source>
      <translation>Jméno:</translation>
    </message>
    <message>
      <source>Local URL</source>
      <translation>Místní URL</translation>
    </message>
    <message>
      <source>Forward To</source>
      <translation>Přeposlat na</translation>
    </message>
    <message>
      <source>API Key</source>
      <translation>API klíč</translation>
    </message>
    <message>
      <source>Confidence threshold:</source>
      <translation>Práh spolehlivosti:</translation>
    </message>
    <message>
      <source>Profiles</source>
      <translation>Profily</translation>
    </message>
    <message>
      <source>1. Configure your profile below (or use the default)</source>
      <translation>1. Nakonfigurujte svůj profil níže (nebo použijte výchozí)</translation>
    </message>
    <message>
      <source>2. Point your LLM client (Claude Code, OpenClaw, etc.) at the Local URL shown below</source>
      <translation>2. Namiřte svého LLM klienta (Claude Code, OpenClaw atd.) na níže zobrazenou místní URL</translation>
    </message>
    <message>
      <source>3. We sit between your client and real API. Everything stays on your machine. Sensitive data is redacted locally before any request leaves your computer ensuring your data never touches our server</source>
      <translation>3. Sedíme mezi vaším klientem a reálným API. Vše zůstává na vašem stroji. Citlivá data se redigují lokálně dříve, než jakýkoli požadavek opustí váš počítač, což zajišťuje, že vaše data se nikdy nedostanou na náš server</translation>
    </message>
    <message>
      <source>The address your LLM client points at</source>
      <translation>Adresa, na kterou váš LLM klient ukazuje</translation>
    </message>
    <message>
      <source>The real API endpoint that receives your redacted requests</source>
      <translation>Skutečný koncový bod API, který přijímá vaše redigované požadavky</translation>
    </message>
    <message>
      <source>AI-powered detection runs locally as an additional layer of defense. May miss data or over-redact. Use Regex Patterns and Keywords below for deterministic redaction.</source>
      <translation>Detekce poháněná umělou inteligencí běží lokálně jako dodatečná vrstva obrany. Může přehlédnout data nebo příliš redigovat. Použijte níže uvedené Regex vzory a Klíčová slova pro deterministickou redigaci.</translation>
    </message>
    <message>
      <source>Expect slightly slower responses when enabled. The model scans every message locally.</source>
      <translation>Po povolení očekávejte mírně pomalejší odpovědi. Model lokálně skenuje každou zprávu.</translation>
    </message>
    <message>
      <source>Text matching these patterns will be redacted before sending to the API</source>
      <translation>Text odpovídající těmto vzorům bude redigován před odesláním do API</translation>
    </message>
    <message>
      <source>Messages containing these words will be flagged for redaction</source>
      <translation>Zprávy obsahující tato slova budou označeny k redigování</translation>
    </message>
    <message>
      <source>Actual redactions detected in the current session.</source>
      <translation>Skutečné redigace zjištěné v aktuální relaci.</translation>
    </message>
    <message>
      <source>No redactions in current session.</source>
      <translation>V aktuální relaci nejsou žádné redigace.</translation>
    </message>
    <message>
      <source>Logs are stored on this PC. Redacted logs may still contain sensitive data that detection missed.</source>
      <translation>Protokoly jsou uloženy na tomto počítači. Redigované protokoly mohou stále obsahovat citlivá data, která nebyla detekována.</translation>
    </message>
    <message>
      <source>e.g., Work OpenAI</source>
      <translation>např., Práce OpenAI</translation>
    </message>
    <message>
      <source>e.g. https://openrouter.ai/api/v1</source>
      <translation>např. https://openrouter.ai/api/v1</translation>
    </message>
    <message>
      <source>e.g. sk-[a-zA-Z0-9]{20,}</source>
      <translation>např. sk-[a-zA-Z0-9]{20,}</translation>
    </message>
    <message>
      <source>e.g. password</source>
      <translation>např. heslo</translation>
    </message>
    <message>
      <source>Add</source>
      <translation>Přidat</translation>
    </message>
    <message>
      <source>Remove</source>
      <translation>Odebrat</translation>
    </message>
    <message>
      <source>Show API key</source>
      <translation>Zobrazit klíč API</translation>
    </message>
    <message>
      <source>Copy</source>
      <translation>Kopírovat</translation>
    </message>
    <message>
      <source>Save</source>
      <translation>Uložit</translation>
    </message>
    <message>
      <source>Use AI model for PII detection</source>
      <translation>Použijte model AI pro detekci PII</translation>
    </message>
    <message>
      <source>Case sensitive</source>
      <translation>Rozlišovat velikost písmen</translation>
    </message>
    <message>
      <source>Require master password</source>
      <translation>Vyžadovat hlavní heslo</translation>
    </message>
    <message>
      <source>Clear statistics</source>
      <translation>Vymazat statistiky</translation>
    </message>
    <message>
      <source>Clear</source>
      <translation>Vymazat</translation>
    </message>
    <message>
      <source>Enable logging</source>
      <translation>Povolit protokolování</translation>
    </message>
    <message>
      <source>Show sensitive information in logs</source>
      <translation>Zobrazovat citlivé informace v protokolech</translation>
    </message>
    <message>
      <source>Open log file</source>
      <translation>Otevřít soubor logu</translation>
    </message>
    <message>
      <source>Open folder</source>
      <translation>Otevřít složku</translation>
    </message>
    <message>
      <source>Delete all logs</source>
      <translation>Smazat všechny logy</translation>
    </message>
    <message>
      <source>Start on Boot</source>
      <translation>Začněte při spuštění</translation>
    </message>
    <message>
      <source>Language</source>
      <translation>Jazyk</translation>
    </message>
    <message>
      <source>System default</source>
      <translation>Výchozí nastavení systému</translation>
    </message>
    <message>
      <source>Master Password</source>
      <translation>Hlavní heslo</translation>
    </message>
    <message>
      <source>Unlock</source>
      <translation>Odemknout</translation>
    </message>
    <message>
      <source>Agent Redactor is locked</source>
      <translation>Agent Redactor je uzamčen</translation>
    </message>
    <message>
      <source>Profile name</source>
      <translation>Název profilu</translation>
    </message>
    <message>
      <source>Proxy port</source>
      <translation>Proxy port</translation>
    </message>
    <message>
      <source>Forward To URL</source>
      <translation>Přeposlat na URL</translation>
    </message>
    <message>
      <source>API key</source>
      <translation>API klíč</translation>
    </message>
    <message>
      <source>Confidence threshold</source>
      <translation>Práh důvěry</translation>
    </message>
    <message>
      <source>New regex pattern</source>
      <translation>Nový vzor regulárního výrazu</translation>
    </message>
    <message>
      <source>New keyword</source>
      <translation>Nové klíčové slovo</translation>
    </message>
    <message>
      <source>Session redactions</source>
      <translation>Redakce relací</translation>
    </message>
    <message>
      <source>Master password</source>
      <translation>Hlavní heslo</translation>
    </message>
    <message>
      <source>Enabled</source>
      <translation>Povoleno</translation>
    </message>
    <message>
      <source>Regex Pattern</source>
      <translation>Regex vzor</translation>
    </message>
    <message>
      <source>Case</source>
      <translation>Velikost písmen</translation>
    </message>
    <message>
      <source>Keyword</source>
      <translation>Klíčové slovo</translation>
    </message>
    <message>
      <source>Account number</source>
      <translation>Číslo účtu</translation>
    </message>
    <message>
      <source>Address</source>
      <translation>Adresa</translation>
    </message>
    <message>
      <source>Date</source>
      <translation>Datum</translation>
    </message>
    <message>
      <source>Email</source>
      <translation>E-mail</translation>
    </message>
    <message>
      <source>Person</source>
      <translation>Osoba</translation>
    </message>
    <message>
      <source>Phone</source>
      <translation>Telefon</translation>
    </message>
    <message>
      <source>URL</source>
      <translation>URL</translation>
    </message>
    <message>
      <source>Secret</source>
      <translation>Tajemství</translation>
    </message>
    <message>
      <source>Requests: %1   PII: %2   Regex: %3   Keywords: %4</source>
      <translation>Požadavky: %1 PII: %2 Regex: %3 Klíčová slova: %4</translation>
    </message>
    <message>
      <source>Engine is not running — retrying…</source>
      <translation>Motor neběží – opakování…</translation>
    </message>
    <message>
      <source>Default</source>
      <translation>Výchozí</translation>
    </message>
    <message>
      <source>Enable pattern</source>
      <translation>Povolit vzor</translation>
    </message>
    <message>
      <source>Regex pattern</source>
      <translation>Regex vzor</translation>
    </message>
    <message>
      <source>Delete</source>
      <translation>Vymazat</translation>
    </message>
    <message>
      <source>Validation Error</source>
      <translation>Chyba ověření</translation>
    </message>
    <message>
      <source>Invalid regex syntax.</source>
      <translation>Neplatná regex syntaxe.</translation>
    </message>
    <message>
      <source>Enable keyword</source>
      <translation>Povolit klíčové slovo</translation>
    </message>
    <message>
      <source>Yes</source>
      <translation>Ano</translation>
    </message>
    <message>
      <source>No</source>
      <translation>Ne</translation>
    </message>
    <message>
      <source>Keyword text</source>
      <translation>Text klíčového slova</translation>
    </message>
    <message>
      <source>Port must be between 1024 and 65535.</source>
      <translation>Port musí být mezi 1024 a 65535.</translation>
    </message>
    <message>
      <source>Port %1 is already used by profile '%2'.</source>
      <translation>Port %1 již používá profil '%2'.</translation>
    </message>
    <message>
      <source>Forward To URL must start with http:// or https://.</source>
      <translation>URL pro přeposlání musí začínat http:// nebo https://.</translation>
    </message>
    <message>
      <source>Confidence threshold must be between 0.0 and 1.0.</source>
      <translation>Prah spolehlivosti musí být mezi 0,0 a 1,0.</translation>
    </message>
    <message>
      <source>Security Warning</source>
      <translation>Bezpečnostní varování</translation>
    </message>
    <message>
      <source>You are using an HTTP (unencrypted) upstream URL. Your API key will be sent in plaintext over the network.</source>
      <translation>Používáte HTTP upstream URL (nešifrované). Váš API klíč bude odeslán po síti jako prostý text.</translation>
    </message>
    <message>
      <source>Error</source>
      <translation>Chyba</translation>
    </message>
    <message>
      <source>The engine rejected the profile. Check the engine log for details.</source>
      <translation>Motor odmítl profil. Podrobnosti najdete v protokolu motoru.</translation>
    </message>
    <message>
      <source>Profile %1</source>
      <translation>Profil %1</translation>
    </message>
    <message>
      <source>The engine rejected the new profile.</source>
      <translation>Motor odmítl nový profil.</translation>
    </message>
    <message>
      <source>Remove Profile</source>
      <translation>Odebrat profil</translation>
    </message>
    <message>
      <source>Are you sure? This operation is permanent.</source>
      <translation>Jste si jisti? Tato operace je trvalá.</translation>
    </message>
    <message>
      <source>Proxy URL copied to clipboard</source>
      <translation>Adresa URL proxy zkopírována do schránky</translation>
    </message>
    <message>
      <source>Port %1 is available</source>
      <translation>Port %1 je dostupný</translation>
    </message>
    <message>
      <source>Port %1 is already in use</source>
      <translation>Port %1 je již používán</translation>
    </message>
    <message>
      <source>Wrong password.</source>
      <translation>Nesprávné heslo.</translation>
    </message>
    <message>
      <source>Show sensitive information</source>
      <translation>Ukažte citlivé informace</translation>
    </message>
    <message>
      <source>Sensitive logging writes raw, unredacted values (including API keys) to the log. Only enable it while debugging.</source>
      <translation>Citlivé protokolování zapisuje do protokolu nezpracované, neredigované hodnoty (včetně klíčů API). Povolte jej pouze při ladění.</translation>
    </message>
    <message>
      <source>Enable logging first.</source>
      <translation>Nejprve povolte protokolování.</translation>
    </message>
    <message>
      <source>Delete all logs?</source>
      <translation>Smazat všechny logy?</translation>
    </message>
    <message>
      <source>This will permanently delete the current log file and all archived session logs. This cannot be undone.</source>
      <translation>Toto trvale smaže aktuální soubor logu a všechny archivované relační logy. Toto nelze vrátit zpět.</translation>
    </message>
    <message>
      <source>Downloading AI model</source>
      <translation>Stahování modelu AI</translation>
    </message>
    <message>
      <source>Retry</source>
      <translation>Zkusit znovu</translation>
    </message>
    <message>
      <source>The PII detection model is downloading (%1%).</source>
      <translation>Stahuje se model detekce PII (%1 %).</translation>
    </message>
    <message>
      <source>The model download failed. Check your internet connection, then retry. PII detection is unavailable until the download completes.</source>
      <translation>Stažení modelu se nezdařilo. Zkontrolujte připojení k internetu a zkuste to znovu. Detekce PII není k dispozici, dokud se stahování nedokončí.</translation>
    </message>
    <message>
      <source>Are you sure you want to quit?</source>
      <translation>Jste si jisti, že chcete ukončit?</translation>
    </message>
    <message>
      <source>If you quit, Agent Redactor will no longer monitor and redact API traffic.</source>
      <translation>Pokud ukončíte, Agent Redactor již nebude sledovat a redigovat API provoz.</translation>
    </message>
    <message>
      <source>Quit Agent Redactor? The engine keeps running in the background.</source>
      <translation>Ukončit Agent Redactor? Motor běží na pozadí.</translation>
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
      <translation>Otevřít Agent Redactor</translation>
    </message>
    <message>
      <source>Start on Boot</source>
      <translation>Začněte při spuštění</translation>
    </message>
    <message>
      <source>Language</source>
      <translation>Jazyk</translation>
    </message>
    <message>
      <source>Quit</source>
      <translation>Ukončit</translation>
    </message>
  </context>
  <context>
    <name>PasswordEnableDialog</name>
    <message>
      <source>Enable password protection</source>
      <translation>Povolit ochranu heslem</translation>
    </message>
    <message>
      <source>Choose a master password for Agent Redactor. It protects your stored API keys on this machine and is unrelated to your login password.</source>
      <translation>Zvolte hlavní heslo pro Agent Redactor. Chrání vaše uložené klíče API na tomto počítači a nesouvisí s vaším přihlašovacím heslem.</translation>
    </message>
    <message>
      <source>New password:</source>
      <translation>Nové heslo:</translation>
    </message>
    <message>
      <source>Confirm password:</source>
      <translation>Potvrďte heslo:</translation>
    </message>
    <message>
      <source>Password must not be empty.</source>
      <translation>Heslo nesmí být prázdné.</translation>
    </message>
    <message>
      <source>Passwords do not match.</source>
      <translation>Hesla se neshodují.</translation>
    </message>
  </context>
  <context>
    <name>PasswordUnlockDialog</name>
    <message>
      <source>Unlock Agent Redactor</source>
      <translation>Odemkněte Agent Redactor</translation>
    </message>
    <message>
      <source>Enter your master password to unlock.</source>
      <translation>Pro odemknutí zadejte své hlavní heslo.</translation>
    </message>
  </context>
</TS>