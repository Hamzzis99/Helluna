// HellunaLobbyLog.h — 로비 시스템 공용 로그 카테고리
// 모든 Lobby .cpp 파일에서 이 헤더를 include하여 LogHellunaLobby 사용
// DEFINE_LOG_CATEGORY(LogHellunaLobby)는 HellunaLobbyGameMode.cpp에서 한 번만 선언

#pragma once

#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogHellunaLobby, Log, All);
