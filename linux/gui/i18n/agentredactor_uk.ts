<?xml version='1.0' encoding='utf-8'?>
<TS version="2.1" language="uk">
  <context>
    <name>MainWindow</name>
    <message>
      <source>Agent Redactor</source>
      <translation>Agent Redactor</translation>
    </message>
    <message>
      <source>Update ready to install</source>
      <translation>Оновлення готове до інсталяції</translation>
    </message>
    <message>
      <source>Agent Redactor %1 has been downloaded. Restart now to apply the update.</source>
      <translation>Agent Redactor %1 завантажено. Перезапустіть зараз, щоб застосувати оновлення.</translation>
    </message>
    <message>
      <source>Restart now</source>
      <translation>Перезапустити зараз</translation>
    </message>
    <message>
      <source>Later</source>
      <translation>Пізніше</translation>
    </message>
    <message>
      <source>Check for updates</source>
      <translation>Перевірити оновлення</translation>
    </message>
    <message>
      <source>You're up to date.</source>
      <translation>У вас найновіша версія.</translation>
    </message>
    <message>
      <source>Couldn't check for updates. Try again later.</source>
      <translation>Не вдалося перевірити оновлення. Спробуйте пізніше.</translation>
    </message>
    <message>
      <source>On</source>
      <translation>Увімкнено</translation>
    </message>
    <message>
      <source>Off</source>
      <translation>Вимкнено</translation>
    </message>
    <message>
      <source>How to use Agent Redactor</source>
      <translation>Як використовувати Agent Redactor</translation>
    </message>
    <message>
      <source>API Proxy</source>
      <translation>API-проксі</translation>
    </message>
    <message>
      <source>AI Powered Detection Model</source>
      <translation>Модель виявлення на основі ШІ</translation>
    </message>
    <message>
      <source>Regex Patterns</source>
      <translation>Шаблони регулярних виразів</translation>
    </message>
    <message>
      <source>Keywords</source>
      <translation>Ключові слова</translation>
    </message>
    <message>
      <source>Password</source>
      <translation>Пароль</translation>
    </message>
    <message>
      <source>Statistics</source>
      <translation>Статистика</translation>
    </message>
    <message>
      <source>Session Redactions</source>
      <translation>Редагування сеансу</translation>
    </message>
    <message>
      <source>Logs</source>
      <translation>Журнали</translation>
    </message>
    <message>
      <source>Settings</source>
      <translation>Налаштування</translation>
    </message>
    <message>
      <source>Name:</source>
      <translation>Ім'я:</translation>
    </message>
    <message>
      <source>Local URL</source>
      <translation>Локальна URL-адреса</translation>
    </message>
    <message>
      <source>Forward To</source>
      <translation>Пересилати на</translation>
    </message>
    <message>
      <source>API Key</source>
      <translation>Ключ API</translation>
    </message>
    <message>
      <source>Confidence threshold:</source>
      <translation>Поріг впевненості:</translation>
    </message>
    <message>
      <source>Profiles</source>
      <translation>Профілі</translation>
    </message>
    <message>
      <source>1. Configure your profile below (or use the default)</source>
      <translation>1. Налаштуйте свій профіль нижче (або використовуйте профіль за замовчуванням)</translation>
    </message>
    <message>
      <source>2. Point your LLM client (Claude Code, OpenClaw, etc.) at the Local URL shown below</source>
      <translation>2. Спрямуйте свій LLM-клієнт (Claude Code, OpenClaw тощо) на локальну URL-адресу, показану нижче</translation>
    </message>
    <message>
      <source>3. We sit between your client and real API. Everything stays on your machine. Sensitive data is redacted locally before any request leaves your computer ensuring your data never touches our server</source>
      <translation>3. Ми знаходимося між вашим клієнтом і справжнім API. Усе залишається на вашому комп'ютері. Конфіденційні дані редагуються локально, перш ніж будь-який запит залишить ваш комп'ютер, гарантуючи, що ваші дані ніколи не потраплять на наш сервер</translation>
    </message>
    <message>
      <source>The address your LLM client points at</source>
      <translation>Адреса, на яку вказує ваш LLM-клієнт</translation>
    </message>
    <message>
      <source>The real API endpoint that receives your redacted requests</source>
      <translation>Справжня кінцева точка API, яка отримує ваші відредаговані запити</translation>
    </message>
    <message>
      <source>AI-powered detection runs locally as an additional layer of defense. May miss data or over-redact. Use Regex Patterns and Keywords below for deterministic redaction.</source>
      <translation>Виявлення на основі ШІ працює локально як додатковий рівень захисту. Може пропустити дані або відредагувати зайве. Використовуйте шаблони регулярних виразів і ключові слова нижче для детермінованого редагування.</translation>
    </message>
    <message>
      <source>Expect slightly slower responses when enabled. The model scans every message locally.</source>
      <translation>Очікуйте дещо повільніших відповідей увімкненого режиму. Модель сканує кожне повідомлення локально.</translation>
    </message>
    <message>
      <source>Text matching these patterns will be redacted before sending to the API</source>
      <translation>Текст, що відповідає цим шаблонам, буде відредаговано перед відправкою до API</translation>
    </message>
    <message>
      <source>Messages containing these words will be flagged for redaction</source>
      <translation>Повідомлення, що містять ці слова, будуть позначені для редагування</translation>
    </message>
    <message>
      <source>Actual redactions detected in the current session.</source>
      <translation>Фактичні редагування, виявлені в поточному сеансі.</translation>
    </message>
    <message>
      <source>No redactions in current session.</source>
      <translation>У поточному сеансі немає редагувань.</translation>
    </message>
    <message>
      <source>Logs are stored on this PC. Redacted logs may still contain sensitive data that detection missed.</source>
      <translation>Журнали зберігаються на цьому ПК. Відредаговані журнали все ще можуть містити конфіденційні дані, які не були виявлені.</translation>
    </message>
    <message>
      <source>e.g., Work OpenAI</source>
      <translation>напр., Робота OpenAI</translation>
    </message>
    <message>
      <source>e.g. https://openrouter.ai/api/v1</source>
      <translation>напр. https://openrouter.ai/api/v1</translation>
    </message>
    <message>
      <source>e.g. sk-[a-zA-Z0-9]{20,}</source>
      <translation>напр. sk-[a-zA-Z0-9]{20,}</translation>
    </message>
    <message>
      <source>e.g. password</source>
      <translation>напр. пароль</translation>
    </message>
    <message>
      <source>Add</source>
      <translation>Додати</translation>
    </message>
    <message>
      <source>Remove</source>
      <translation>Видалити</translation>
    </message>
    <message>
      <source>Show API key</source>
      <translation>Показати ключ API</translation>
    </message>
    <message>
      <source>Copy</source>
      <translation>Копіювати</translation>
    </message>
    <message>
      <source>Save</source>
      <translation>зберегти</translation>
    </message>
    <message>
      <source>Use AI model for PII detection</source>
      <translation>Використовуйте модель ШІ для виявлення ідентифікаційної інформації</translation>
    </message>
    <message>
      <source>Case sensitive</source>
      <translation>Враховувати регістр</translation>
    </message>
    <message>
      <source>Require master password</source>
      <translation>Вимагати головний пароль</translation>
    </message>
    <message>
      <source>Clear statistics</source>
      <translation>Чітка статистика</translation>
    </message>
    <message>
      <source>Clear</source>
      <translation>Очистити</translation>
    </message>
    <message>
      <source>Enable logging</source>
      <translation>Увімкнути журналювання</translation>
    </message>
    <message>
      <source>Show sensitive information in logs</source>
      <translation>Показувати конфіденційну інформацію в журналах</translation>
    </message>
    <message>
      <source>Open log file</source>
      <translation>Відкрити файл журналу</translation>
    </message>
    <message>
      <source>Open folder</source>
      <translation>Відкрити папку</translation>
    </message>
    <message>
      <source>Delete all logs</source>
      <translation>Видалити всі журнали</translation>
    </message>
    <message>
      <source>Start on Boot</source>
      <translation>Почніть із завантаження</translation>
    </message>
    <message>
      <source>Language</source>
      <translation>Мова</translation>
    </message>
    <message>
      <source>System default</source>
      <translation>Системна за замовчуванням</translation>
    </message>
    <message>
      <source>Master Password</source>
      <translation>Головний пароль</translation>
    </message>
    <message>
      <source>Unlock</source>
      <translation>Розблокувати</translation>
    </message>
    <message>
      <source>Agent Redactor is locked</source>
      <translation>Редактор агента заблоковано</translation>
    </message>
    <message>
      <source>Profile name</source>
      <translation>Ім'я профілю</translation>
    </message>
    <message>
      <source>Proxy port</source>
      <translation>Порт проксі</translation>
    </message>
    <message>
      <source>Forward To URL</source>
      <translation>Переслати на URL</translation>
    </message>
    <message>
      <source>API key</source>
      <translation>Ключ API</translation>
    </message>
    <message>
      <source>Confidence threshold</source>
      <translation>Поріг довіри</translation>
    </message>
    <message>
      <source>New regex pattern</source>
      <translation>Новий шаблон регулярного виразу</translation>
    </message>
    <message>
      <source>New keyword</source>
      <translation>Нове ключове слово</translation>
    </message>
    <message>
      <source>Session redactions</source>
      <translation>Редакції сесії</translation>
    </message>
    <message>
      <source>Master password</source>
      <translation>Головний пароль</translation>
    </message>
    <message>
      <source>Enabled</source>
      <translation>Увімкнено</translation>
    </message>
    <message>
      <source>Regex Pattern</source>
      <translation>Шаблон регулярного виразу</translation>
    </message>
    <message>
      <source>Case</source>
      <translation>Регістр</translation>
    </message>
    <message>
      <source>Keyword</source>
      <translation>Ключове слово</translation>
    </message>
    <message>
      <source>Account number</source>
      <translation>Номер рахунку</translation>
    </message>
    <message>
      <source>Address</source>
      <translation>Адреса</translation>
    </message>
    <message>
      <source>Date</source>
      <translation>Дата</translation>
    </message>
    <message>
      <source>Email</source>
      <translation>Електронна пошта</translation>
    </message>
    <message>
      <source>Person</source>
      <translation>Особа</translation>
    </message>
    <message>
      <source>Phone</source>
      <translation>Телефон</translation>
    </message>
    <message>
      <source>URL</source>
      <translation>URL</translation>
    </message>
    <message>
      <source>Secret</source>
      <translation>Секрет</translation>
    </message>
    <message>
      <source>Requests: %1   PII: %2   Regex: %3   Keywords: %4</source>
      <translation>Запити: %1 ідентифікаційна інформація: %2 Регулярний вираз: %3 Ключові слова: %4</translation>
    </message>
    <message>
      <source>Engine is not running — retrying…</source>
      <translation>Двигун не працює — повторна спроба…</translation>
    </message>
    <message>
      <source>Default</source>
      <translation>За замовчуванням</translation>
    </message>
    <message>
      <source>Enable pattern</source>
      <translation>Увімкнути шаблон</translation>
    </message>
    <message>
      <source>Regex pattern</source>
      <translation>Шаблон регулярного виразу</translation>
    </message>
    <message>
      <source>Delete</source>
      <translation>Видалити</translation>
    </message>
    <message>
      <source>Validation Error</source>
      <translation>Помилка перевірки</translation>
    </message>
    <message>
      <source>Invalid regex syntax.</source>
      <translation>Неправильний синтаксис регулярного виразу.</translation>
    </message>
    <message>
      <source>This entry already exists.</source>
      <translation>Цей елемент вже існує.</translation>
    </message>
    <message>
      <source>Enable keyword</source>
      <translation>Увімкнути ключове слово</translation>
    </message>
    <message>
      <source>Yes</source>
      <translation>Так</translation>
    </message>
    <message>
      <source>No</source>
      <translation>Ні</translation>
    </message>
    <message>
      <source>Keyword text</source>
      <translation>Текст ключового слова</translation>
    </message>
    <message>
      <source>Port must be between 1024 and 65535.</source>
      <translation>Порт має бути в діапазоні від 1024 до 65535.</translation>
    </message>
    <message>
      <source>Port %1 is already used by profile '%2'.</source>
      <translation>Порт %1 уже використовується профілем '%2'.</translation>
    </message>
    <message>
      <source>Forward To URL must start with http:// or https://.</source>
      <translation>URL для пересилання має починатися з http:// або https://.</translation>
    </message>
    <message>
      <source>Confidence threshold must be between 0.0 and 1.0.</source>
      <translation>Поріг впевненості має бути в діапазоні від 0,0 до 1,0.</translation>
    </message>
    <message>
      <source>Security Warning</source>
      <translation>Попередження безпеки</translation>
    </message>
    <message>
      <source>You are using an HTTP (unencrypted) upstream URL. Your API key will be sent in plaintext over the network.</source>
      <translation>Ви використовуєте HTTP URL вищого рівня (незашифрований). Ваш ключ API буде надіслано відкритим текстом через мережу.</translation>
    </message>
    <message>
      <source>Error</source>
      <translation>Помилка</translation>
    </message>
    <message>
      <source>The engine rejected the profile. Check the engine log for details.</source>
      <translation>Двигун відхилив профіль. Подробиці перевірте в журналі двигуна.</translation>
    </message>
    <message>
      <source>Profile %1</source>
      <translation>Профіль %1</translation>
    </message>
    <message>
      <source>The engine rejected the new profile.</source>
      <translation>Двигун відхилив новий профіль.</translation>
    </message>
    <message>
      <source>Remove Profile</source>
      <translation>Видалити профіль</translation>
    </message>
    <message>
      <source>Are you sure? This operation is permanent.</source>
      <translation>Ви впевнені? Ця операція є незворотною.</translation>
    </message>
    <message>
      <source>Proxy URL copied to clipboard</source>
      <translation>URL-адресу проксі-сервера скопійовано в буфер обміну</translation>
    </message>
    <message>
      <source>Port %1 is available</source>
      <translation>Порт %1 доступний</translation>
    </message>
    <message>
      <source>Port %1 is already in use</source>
      <translation>Порт %1 уже використовується</translation>
    </message>
    <message>
      <source>Wrong password.</source>
      <translation>Неправильний пароль.</translation>
    </message>
    <message>
      <source>Show sensitive information</source>
      <translation>Показати конфіденційну інформацію</translation>
    </message>
    <message>
      <source>Sensitive logging writes raw, unredacted values (including API keys) to the log. Only enable it while debugging.</source>
      <translation>Конфіденційне журналювання записує невідредаговані значення (включно з ключами API) до журналу. Увімкніть його лише під час налагодження.</translation>
    </message>
    <message>
      <source>Enable logging first.</source>
      <translation>Спочатку увімкніть журналювання.</translation>
    </message>
    <message>
      <source>Delete all logs?</source>
      <translation>Видалити всі журнали?</translation>
    </message>
    <message>
      <source>This will permanently delete the current log file and all archived session logs. This cannot be undone.</source>
      <translation>Це назавжди видалить поточний файл журналу та всі архівовані журнали сеансів. Це не можна скасувати.</translation>
    </message>
    <message>
      <source>Downloading AI model</source>
      <translation>Завантаження моделі ШІ</translation>
    </message>
    <message>
      <source>Retry</source>
      <translation>Повторити</translation>
    </message>
    <message>
      <source>The PII detection model is downloading (%1%).</source>
      <translation>Модель виявлення ідентифікаційної інформації завантажується (%1%).</translation>
    </message>
    <message>
      <source>The model download failed. Check your internet connection, then retry. PII detection is unavailable until the download completes.</source>
      <translation>Не вдалося завантажити модель. Перевірте підключення до Інтернету та повторіть спробу. Виявлення PII недоступне, доки завантаження не завершиться.</translation>
    </message>
    <message>
      <source>Are you sure you want to quit?</source>
      <translation>Ви впевнені, що хочете вийти?</translation>
    </message>
    <message>
      <source>If you quit, Agent Redactor will no longer monitor and redact API traffic.</source>
      <translation>Якщо ви вийдете, Agent Redactor більше не контролюватиме та не редагуватиме API-трафік.</translation>
    </message>
    <message>
      <source>Quit Agent Redactor? The engine keeps running in the background.</source>
      <translation>Вийти з Agent Redactor? Двигун продовжує працювати у фоновому режимі.</translation>
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
      <translation>Відкрити Agent Redactor</translation>
    </message>
    <message>
      <source>Start on Boot</source>
      <translation>Почніть із завантаження</translation>
    </message>
    <message>
      <source>Language</source>
      <translation>Мова</translation>
    </message>
    <message>
      <source>Quit</source>
      <translation>Вийти</translation>
    </message>
  </context>
  <context>
    <name>PasswordEnableDialog</name>
    <message>
      <source>Enable password protection</source>
      <translation>Увімкнути захист паролем</translation>
    </message>
    <message>
      <source>Choose a master password for Agent Redactor. It protects your stored API keys on this machine and is unrelated to your login password.</source>
      <translation>Виберіть головний пароль для Agent Redactor. Він захищає ключі API, які зберігаються на цій машині, і не пов’язаний з вашим паролем для входу.</translation>
    </message>
    <message>
      <source>New password:</source>
      <translation>Новий пароль:</translation>
    </message>
    <message>
      <source>Confirm password:</source>
      <translation>Підтвердьте пароль:</translation>
    </message>
    <message>
      <source>Password must not be empty.</source>
      <translation>Пароль не повинен бути порожнім.</translation>
    </message>
    <message>
      <source>Passwords do not match.</source>
      <translation>Паролі не збігаються.</translation>
    </message>
  </context>
  <context>
    <name>PasswordUnlockDialog</name>
    <message>
      <source>Unlock Agent Redactor</source>
      <translation>Розблокувати редактор агентів</translation>
    </message>
    <message>
      <source>Enter your master password to unlock.</source>
      <translation>Введіть головний пароль, щоб розблокувати.</translation>
    </message>
  </context>
</TS>