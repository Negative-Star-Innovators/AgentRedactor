<?xml version='1.0' encoding='utf-8'?>
<TS version="2.1" language="ko">
  <context>
    <name>MainWindow</name>
    <message>
      <source>Agent Redactor</source>
      <translation>Agent Redactor</translation>
    </message>
    <message>
      <source>Update ready to install</source>
      <translation>업데이트 설치 준비 완료</translation>
    </message>
    <message>
      <source>Agent Redactor %1 has been downloaded. Restart now to apply the update.</source>
      <translation>Agent Redactor %1이(가) 다운로드되었습니다. 지금 다시 시작하여 업데이트를 적용하세요.</translation>
    </message>
    <message>
      <source>Restart now</source>
      <translation>지금 다시 시작</translation>
    </message>
    <message>
      <source>Later</source>
      <translation>나중에</translation>
    </message>
    <message>
      <source>Check for updates</source>
      <translation>업데이트 확인</translation>
    </message>
    <message>
      <source>You're up to date.</source>
      <translation>최신 버전입니다.</translation>
    </message>
    <message>
      <source>Couldn't check for updates. Try again later.</source>
      <translation>업데이트를 확인할 수 없습니다. 나중에 다시 시도하세요.</translation>
    </message>
    <message>
      <source>On</source>
      <translation>~에</translation>
    </message>
    <message>
      <source>Off</source>
      <translation>끄다</translation>
    </message>
    <message>
      <source>How to use Agent Redactor</source>
      <translation>Agent Redactor 사용 방법</translation>
    </message>
    <message>
      <source>API Proxy</source>
      <translation>API 프록시</translation>
    </message>
    <message>
      <source>AI Powered Detection Model</source>
      <translation>AI 기반 검출 모델</translation>
    </message>
    <message>
      <source>Regex Patterns</source>
      <translation>정규식 패턴</translation>
    </message>
    <message>
      <source>Keywords</source>
      <translation>키워드</translation>
    </message>
    <message>
      <source>Password</source>
      <translation>암호</translation>
    </message>
    <message>
      <source>Statistics</source>
      <translation>통계</translation>
    </message>
    <message>
      <source>Session Redactions</source>
      <translation>세션 삭제</translation>
    </message>
    <message>
      <source>Logs</source>
      <translation>로그</translation>
    </message>
    <message>
      <source>Settings</source>
      <translation>설정</translation>
    </message>
    <message>
      <source>Name:</source>
      <translation>이름:</translation>
    </message>
    <message>
      <source>Local URL</source>
      <translation>로컬 URL</translation>
    </message>
    <message>
      <source>Forward To</source>
      <translation>전달 대상</translation>
    </message>
    <message>
      <source>API Key</source>
      <translation>API 키</translation>
    </message>
    <message>
      <source>Confidence threshold:</source>
      <translation>신뢰도 임계값:</translation>
    </message>
    <message>
      <source>Profiles</source>
      <translation>프로필</translation>
    </message>
    <message>
      <source>1. Configure your profile below (or use the default)</source>
      <translation>1. 아래에서 프로필을 구성하세요(또는 기본값 사용)</translation>
    </message>
    <message>
      <source>2. Point your LLM client (Claude Code, OpenClaw, etc.) at the Local URL shown below</source>
      <translation>2. LLM 클라이언트(Claude Code, OpenClaw 등)를 아래에 표시된 로컬 URL로 지정하세요</translation>
    </message>
    <message>
      <source>3. We sit between your client and real API. Everything stays on your machine. Sensitive data is redacted locally before any request leaves your computer ensuring your data never touches our server</source>
      <translation>3. 클라이언트와 실제 API 사이에 위치합니다. 모든 것은 사용자의 컴퓨터에 그대로 있습니다. 민감한 데이터는 요청이 컴퓨터를 떠나기 전에 로컬에서 삭제되어 데이터가 당사 서버에 닿지 않습니다</translation>
    </message>
    <message>
      <source>The address your LLM client points at</source>
      <translation>LLM 클라이언트가 가리키는 주소</translation>
    </message>
    <message>
      <source>The real API endpoint that receives your redacted requests</source>
      <translation>삭제된 요청을 받는 실제 API 엔드포인트</translation>
    </message>
    <message>
      <source>AI-powered detection runs locally as an additional layer of defense. May miss data or over-redact. Use Regex Patterns and Keywords below for deterministic redaction.</source>
      <translation>AI 기반 검출은 추가 방어 계층으로 로컬에서 실행됩니다. 데이터를 놓치거나 과도하게 삭제할 수 있습니다. 결정론적 삭제를 위해 아래의 정규식 패턴과 키워드를 사용하세요.</translation>
    </message>
    <message>
      <source>Expect slightly slower responses when enabled. The model scans every message locally.</source>
      <translation>사용 시 응답이 약간 느려질 수 있습니다. 모델은 모든 메시지를 로컬에서 검사합니다.</translation>
    </message>
    <message>
      <source>Text matching these patterns will be redacted before sending to the API</source>
      <translation>이러한 패턴과 일치하는 텍스트는 API로 전송되기 전에 삭제됩니다</translation>
    </message>
    <message>
      <source>Messages containing these words will be flagged for redaction</source>
      <translation>이러한 단어가 포함된 메시지는 삭제 대상으로 표시됩니다</translation>
    </message>
    <message>
      <source>Actual redactions detected in the current session.</source>
      <translation>현재 세션에서 감지된 실제 삭제 항목입니다.</translation>
    </message>
    <message>
      <source>No redactions in current session.</source>
      <translation>현재 세션에 삭제 항목이 없습니다.</translation>
    </message>
    <message>
      <source>Logs are stored on this PC. Redacted logs may still contain sensitive data that detection missed.</source>
      <translation>로그는 이 PC에 저장됩니다. 수정된 로그에는 탐지에서 놓친 민감한 데이터가 여전히 포함될 수 있습니다.</translation>
    </message>
    <message>
      <source>e.g., Work OpenAI</source>
      <translation>예: 업무용 OpenAI</translation>
    </message>
    <message>
      <source>e.g. https://openrouter.ai/api/v1</source>
      <translation>예: https://openrouter.ai/api/v1</translation>
    </message>
    <message>
      <source>e.g. sk-[a-zA-Z0-9]{20,}</source>
      <translation>예: sk-[a-zA-Z0-9]{20,}</translation>
    </message>
    <message>
      <source>e.g. password</source>
      <translation>예: password</translation>
    </message>
    <message>
      <source>Add</source>
      <translation>추가</translation>
    </message>
    <message>
      <source>Remove</source>
      <translation>제거</translation>
    </message>
    <message>
      <source>Show API key</source>
      <translation>API 키 표시</translation>
    </message>
    <message>
      <source>Copy</source>
      <translation>복사</translation>
    </message>
    <message>
      <source>Save</source>
      <translation>구하다</translation>
    </message>
    <message>
      <source>Use AI model for PII detection</source>
      <translation>PII 감지를 위해 AI 모델 사용</translation>
    </message>
    <message>
      <source>Case sensitive</source>
      <translation>대소문자 구분</translation>
    </message>
    <message>
      <source>Require master password</source>
      <translation>마스터 비밀번호 필요</translation>
    </message>
    <message>
      <source>Clear statistics</source>
      <translation>통계 지우기</translation>
    </message>
    <message>
      <source>Clear</source>
      <translation>지우기</translation>
    </message>
    <message>
      <source>Enable logging</source>
      <translation>로깅 활성화</translation>
    </message>
    <message>
      <source>Show sensitive information in logs</source>
      <translation>로그에 민감한 정보 표시</translation>
    </message>
    <message>
      <source>Open log file</source>
      <translation>로그 파일 열기</translation>
    </message>
    <message>
      <source>Open folder</source>
      <translation>폴더 열기</translation>
    </message>
    <message>
      <source>Delete all logs</source>
      <translation>모든 로그 삭제</translation>
    </message>
    <message>
      <source>Start on Boot</source>
      <translation>부팅 시 시작</translation>
    </message>
    <message>
      <source>Language</source>
      <translation>언어</translation>
    </message>
    <message>
      <source>System default</source>
      <translation>시스템 기본값</translation>
    </message>
    <message>
      <source>Master Password</source>
      <translation>마스터 암호</translation>
    </message>
    <message>
      <source>Unlock</source>
      <translation>잠금 해제</translation>
    </message>
    <message>
      <source>Agent Redactor is locked</source>
      <translation>에이전트 편집자가 잠겨 있습니다.</translation>
    </message>
    <message>
      <source>Profile name</source>
      <translation>프로필 이름</translation>
    </message>
    <message>
      <source>Proxy port</source>
      <translation>프록시 포트</translation>
    </message>
    <message>
      <source>Forward To URL</source>
      <translation>URL로 전달</translation>
    </message>
    <message>
      <source>API key</source>
      <translation>API 키</translation>
    </message>
    <message>
      <source>Confidence threshold</source>
      <translation>신뢰도 임계값</translation>
    </message>
    <message>
      <source>New regex pattern</source>
      <translation>새로운 정규식 패턴</translation>
    </message>
    <message>
      <source>New keyword</source>
      <translation>새 키워드</translation>
    </message>
    <message>
      <source>Session redactions</source>
      <translation>세션 수정</translation>
    </message>
    <message>
      <source>Master password</source>
      <translation>마스터 비밀번호</translation>
    </message>
    <message>
      <source>Enabled</source>
      <translation>사용</translation>
    </message>
    <message>
      <source>Regex Pattern</source>
      <translation>정규식 패턴</translation>
    </message>
    <message>
      <source>Case</source>
      <translation>대소문자</translation>
    </message>
    <message>
      <source>Keyword</source>
      <translation>키워드</translation>
    </message>
    <message>
      <source>Account number</source>
      <translation>계좌 번호</translation>
    </message>
    <message>
      <source>Address</source>
      <translation>주소</translation>
    </message>
    <message>
      <source>Date</source>
      <translation>날짜</translation>
    </message>
    <message>
      <source>Email</source>
      <translation>이메일</translation>
    </message>
    <message>
      <source>Person</source>
      <translation>사람</translation>
    </message>
    <message>
      <source>Phone</source>
      <translation>전화</translation>
    </message>
    <message>
      <source>URL</source>
      <translation>URL</translation>
    </message>
    <message>
      <source>Secret</source>
      <translation>비밀</translation>
    </message>
    <message>
      <source>Requests: %1   PII: %2   Regex: %3   Keywords: %4</source>
      <translation>요청: %1 PII: %2 정규식: %3 키워드: %4</translation>
    </message>
    <message>
      <source>Engine is not running — retrying…</source>
      <translation>엔진이 실행되고 있지 않습니다. 다시 시도하는 중입니다…</translation>
    </message>
    <message>
      <source>Default</source>
      <translation>기본값</translation>
    </message>
    <message>
      <source>Enable pattern</source>
      <translation>패턴 활성화</translation>
    </message>
    <message>
      <source>Regex pattern</source>
      <translation>정규식 패턴</translation>
    </message>
    <message>
      <source>Delete</source>
      <translation>삭제</translation>
    </message>
    <message>
      <source>Validation Error</source>
      <translation>유효성 검사 오류</translation>
    </message>
    <message>
      <source>Invalid regex syntax.</source>
      <translation>정규식 구문이 잘못되었습니다.</translation>
    </message>
    <message>
      <source>Enable keyword</source>
      <translation>키워드 활성화</translation>
    </message>
    <message>
      <source>Yes</source>
      <translation>예</translation>
    </message>
    <message>
      <source>No</source>
      <translation>아니요</translation>
    </message>
    <message>
      <source>Keyword text</source>
      <translation>키워드 텍스트</translation>
    </message>
    <message>
      <source>Port must be between 1024 and 65535.</source>
      <translation>포트는 1024에서 65535 사이여야 합니다.</translation>
    </message>
    <message>
      <source>Port %1 is already used by profile '%2'.</source>
      <translation>포트 %1은(는) '%2' 프로필에서 이미 사용 중입니다.</translation>
    </message>
    <message>
      <source>Forward To URL must start with http:// or https://.</source>
      <translation>전달 대상 URL은 http:// 또는 https://로 시작해야 합니다.</translation>
    </message>
    <message>
      <source>Confidence threshold must be between 0.0 and 1.0.</source>
      <translation>신뢰도 임계값은 0.0에서 1.0 사이여야 합니다.</translation>
    </message>
    <message>
      <source>Security Warning</source>
      <translation>보안 경고</translation>
    </message>
    <message>
      <source>You are using an HTTP (unencrypted) upstream URL. Your API key will be sent in plaintext over the network.</source>
      <translation>HTTP(암호화되지 않은) 업스트림 URL을 사용하고 있습니다. API 키가 네트워크를 통해 일반 텍스트로 전송됩니다.</translation>
    </message>
    <message>
      <source>Error</source>
      <translation>오류</translation>
    </message>
    <message>
      <source>The engine rejected the profile. Check the engine log for details.</source>
      <translation>엔진이 프로필을 거부했습니다. 자세한 내용은 엔진 로그를 확인하세요.</translation>
    </message>
    <message>
      <source>Profile %1</source>
      <translation>프로필%1</translation>
    </message>
    <message>
      <source>The engine rejected the new profile.</source>
      <translation>엔진이 새 프로필을 거부했습니다.</translation>
    </message>
    <message>
      <source>Remove Profile</source>
      <translation>프로필 제거</translation>
    </message>
    <message>
      <source>Are you sure? This operation is permanent.</source>
      <translation>계속하시겠습니까? 이 작업은 되돌릴 수 없습니다.</translation>
    </message>
    <message>
      <source>Proxy URL copied to clipboard</source>
      <translation>프록시 URL이 클립보드에 복사되었습니다.</translation>
    </message>
    <message>
      <source>Port %1 is available</source>
      <translation>포트 %1 사용 가능</translation>
    </message>
    <message>
      <source>Port %1 is already in use</source>
      <translation>포트 %1이(가) 이미 사용 중입니다</translation>
    </message>
    <message>
      <source>Wrong password.</source>
      <translation>비밀번호가 잘못되었습니다.</translation>
    </message>
    <message>
      <source>Show sensitive information</source>
      <translation>민감한 정보 표시</translation>
    </message>
    <message>
      <source>Sensitive logging writes raw, unredacted values (including API keys) to the log. Only enable it while debugging.</source>
      <translation>민감한 로깅은 수정되지 않은 원시 값(API 키 포함)을 로그에 기록합니다. 디버깅하는 동안에만 활성화하십시오.</translation>
    </message>
    <message>
      <source>Enable logging first.</source>
      <translation>먼저 로깅을 활성화하세요.</translation>
    </message>
    <message>
      <source>Delete all logs?</source>
      <translation>모든 로그를 삭제하시겠습니까?</translation>
    </message>
    <message>
      <source>This will permanently delete the current log file and all archived session logs. This cannot be undone.</source>
      <translation>현재 로그 파일과 모든 보관된 세션 로그가 영구적으로 삭제됩니다. 이 작업은 취소할 수 없습니다.</translation>
    </message>
    <message>
      <source>Downloading AI model</source>
      <translation>AI 모델 다운로드 중</translation>
    </message>
    <message>
      <source>Retry</source>
      <translation>다시 시도</translation>
    </message>
    <message>
      <source>The PII detection model is downloading (%1%).</source>
      <translation>PII 탐지 모델을 다운로드 중입니다(%1%).</translation>
    </message>
    <message>
      <source>The model download failed. Check your internet connection, then retry. PII detection is unavailable until the download completes.</source>
      <translation>모델 다운로드에 실패했습니다. 인터넷 연결을 확인한 후 다시 시도하세요. 다운로드가 완료될 때까지 PII 감지를 사용할 수 없습니다.</translation>
    </message>
    <message>
      <source>Are you sure you want to quit?</source>
      <translation>종료하시겠습니까?</translation>
    </message>
    <message>
      <source>If you quit, Agent Redactor will no longer monitor and redact API traffic.</source>
      <translation>종료하면 Agent Redactor가 API 트래픽을 더 이상 모니터링하고 삭제하지 않습니다.</translation>
    </message>
    <message>
      <source>Quit Agent Redactor? The engine keeps running in the background.</source>
      <translation>Agent Redactor를 종료하시겠습니까? 엔진은 백그라운드에서 계속 실행됩니다.</translation>
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
      <translation>Agent Redactor 열기</translation>
    </message>
    <message>
      <source>Start on Boot</source>
      <translation>부팅 시 시작</translation>
    </message>
    <message>
      <source>Language</source>
      <translation>언어</translation>
    </message>
    <message>
      <source>Quit</source>
      <translation>종료</translation>
    </message>
  </context>
  <context>
    <name>PasswordEnableDialog</name>
    <message>
      <source>Enable password protection</source>
      <translation>비밀번호 보호 활성화</translation>
    </message>
    <message>
      <source>Choose a master password for Agent Redactor. It protects your stored API keys on this machine and is unrelated to your login password.</source>
      <translation>Agent Redactor의 마스터 비밀번호를 선택하세요. 이는 이 시스템에 저장된 API 키를 보호하며 로그인 비밀번호와 관련이 없습니다.</translation>
    </message>
    <message>
      <source>New password:</source>
      <translation>새 비밀번호:</translation>
    </message>
    <message>
      <source>Confirm password:</source>
      <translation>비밀번호 확인:</translation>
    </message>
    <message>
      <source>Password must not be empty.</source>
      <translation>비밀번호는 비워둘 수 없습니다.</translation>
    </message>
    <message>
      <source>Passwords do not match.</source>
      <translation>비밀번호가 일치하지 않습니다.</translation>
    </message>
  </context>
  <context>
    <name>PasswordUnlockDialog</name>
    <message>
      <source>Unlock Agent Redactor</source>
      <translation>에이전트 편집자 잠금 해제</translation>
    </message>
    <message>
      <source>Enter your master password to unlock.</source>
      <translation>잠금을 해제하려면 마스터 비밀번호를 입력하세요.</translation>
    </message>
  </context>
</TS>