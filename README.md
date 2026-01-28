<details> <summary>🖥️ (로그인창) 전체 구현 로드맵</summary>


Note: 이 섹션은 서버 접속부터 로그인 검증, 게임 진입까지의 전 과정을 포함합니다.

<details> <summary>📂 Phase 1: 기본 구조 세팅 (완료)</summary>

[x] Step 1. LoginLevel 맵 생성

배경 없는 빈 레벨 구성

[x] Step 2. 폴더 구조 생성

Source/Helluna/Public(Private)/Login/

Source/Helluna/Public(Private)/Player/

</details>

<details> <summary>⚙️ Phase 2: 핵심 클래스 생성 (완료)</summary>

[x] Step 3. HellunaPlayerState 생성

PlayerUniqueId (FString, Replicated)

bIsLoggedIn (bool, Replicated)

[x] Step 4. HellunaAccountSaveGame 생성

FHellunaAccountData 구조체 정의

TMap<FString, FHellunaAccountData> Accounts

HasAccount(), ValidatePassword(), CreateAccount() 구현

[x] Step 5. HellunaLoginController 생성

로그인 UI 표시 로직

Server_RequestLogin() / Client_LoginResult() RPC 틀 작성

[x] Step 6. HellunaLoginGameMode 생성

TSet<FString> LoggedInPlayerIds

계정 로드/저장 및 Seamless Travel 설정

</details>

<details> <summary>🎨 Phase 3: UI 위젯 생성 (완료)</summary>

[x] Step 7. HellunaLoginWidget (C++)

IP, ID, PW 입력용 EditableText 바인딩

접속/로그인 버튼 델리게이트 연결

[x] Step 8. WBP_LoginWidget (Blueprint)

C++ 클래스 상속 및 UI 스타일링

[x] Step 9. 로딩 화면 위젯

</details>

<details open> <summary>🔗 Phase 4: 로직 연결 (완료)</summary>

[x] Step 10. IP 입력 → 서버 접속 로직

Open IP:Port 콘솔 명령 실행 및 접속 상태 감지

[x] Step 11. 로그인 버튼 → 검증 로직

Server_RequestLogin() 호출 → GameMode 데이터 검증

[x] Step 12. 로그인 성공 → 맵 이동

ServerTravel("GihyeonMap") 실행 및 데이터 유지 확인

</details>

<details> <summary>🎮 Phase 5: 게임 맵 연동(완료)</summary>

[x] Step 13. GihyeonMap 설정

PlayerStart 배치 및 HellunaDefenseGameMode 할당

[x] Step 14. 캐릭터 스폰 확인

[x] Step 15. Logout 처리

LoggedInPlayerIds 제거 및 데이터 저장 (인벤토리 연동 대비)

</details>

<details> <summary>⚠️ Phase 6: 예외 처리</summary>

[ ] Step 16. 에러 메시지 UI 처리 (비밀번호 불일치, 중복 접속 등)

[ ] Step 17. 연결 끊김 처리 (40초 타임아웃 및 로그인 창 복귀)

</details>

<details> <summary>🧪 Phase 7: 테스트</summary>

[ ] Step 18. 단일 클라이언트 (접속 → 로그인 → 진입)

[ ] Step 19. 멀티 클라이언트 (3인 동시 접속 및 중복 ID 차단)

[ ] Step 20. 데이터 영속성 (서버 재시작 후 계정 유지 확인)

</details>

</details>
