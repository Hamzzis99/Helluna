# Lobby Return URL Fix

## 요약
- 게임 서버 실행 인자에서 `-LobbyURL=127.0.0.1:7777` 하드코딩을 제거하는 방향으로 수정했다.
- 로비 복귀 URL이 설정된 경우에만 `-LobbyURL=`를 전달한다.
- 값이 비어 있으면 게임 결과 복귀는 클라이언트 `UMDF_GameInstance::ConnectedServerIP` fallback을 사용한다.

## 운영 가이드
- 일반 원격 클라이언트 환경:
  - `LobbyReturnURL`을 비워둔다.
  - 클라이언트가 최초 접속 때 저장한 `ConnectedServerIP`로 로비 복귀한다.
- 외부 노출 주소를 서버가 명시적으로 내려줘야 하는 환경:
  - `BP_HellunaLobbyGameMode`의 `LobbyReturnURL`을 설정하거나
  - 서버 실행 인자에 `-LobbyReturnURL=host:port`를 추가한다.
