﻿#include "main.h"
#include "MMOServer.h"
#include "db/DBHelper.h"
#include "contents_enviroment.h"

/*
서버
- IOCP
- 맵 800 x 800 	시야 15 x 15 window 20 x 20 
- ID를 갖고 로그인.
- 캐릭터
	경험치와 레벨 HP 을 갖고 있음 
	1레벨 경험치 100, 2레벨 요구 경험치 200, 3레벨 400, 4레벨 800… 
	5초마다 10%의 HP회복 
	HP 0이 되면 경험치 반으로 깍은 후 HP회복 돼서 시작위치로
	모든 정보 DB저장
	화면 상단에 HP와 레벨 표시



― 몬스터 
• 레벨, HP, 이름 
• 종류별로 다른 아이콘으로 화면에 표시 
• Type 
	– Peace: : 때리기 전에는 가만히 
	– Agro : 근처에 10x10 영역에 접근하면 쫓아 오기 
• 이동 
– 고정 : 자기 자리에 가만히 
– 로밍 : 원래 위치에서 20 x 20 공간을 자유로이 이동 
– 장애물은 A* 길찾기로 피한다. 
	• 무찔렀을 때의 경험치 – 레벨 * 5 
– Agro 몬스터 2배, 로밍 몬스터 2배 
	• 배치 좌표, 이름, TYPE, Level등 모든 정보 Script로 저장 
	• 죽은 후 30초 후 부활

- 전투
	몬스터는 처음 인식한 공격 대상을 계속 공격 
	• A키를 누르면 주위 4방향 몬스터 동시 공격 
	• 공격속도는 1초에 한번 
	• 메시지 창에 메시지 표시 
	– “용사가 몬스터 A를 때려서 10의 데미지를 입혔습니다.” 
	– “몬스터A의 공격으로 15의 데미지를 입었습니다.” 
	– “몬스터 A를 무찔러서 250의 경험치를 얻었습니다.” 

― 이동 
	• 1초에 한 칸 이동 
― 장애물 & 몬스터 배치 & 밸런스 
	• 자율적으로 조정 
	• 제일 재미있게 배치한 팀에게 10% 가산점


- NPC
- 길찾기
- DB
- SCRIPT
- 채팅
- 장애물
- 버프
- FSM
- 1초에 한칸

성능 - 동접 7천명 

- 클라이언트
메시지 창 (채팅 가능)

클라
- HP 레벨 표시
- 메시지 창 ( 채팅 가능 )
- 알림 창
*/

#include <memory>

using namespace c2;
#include "db/DBManager.h"

void main()
{
	setlocale(LC_ALL, "");

	g_server = new MMOServer();

	g_server->setup_dump();

	g_server->load_config_using_json(L"config.json");
	
	g_db_manager = new DBManager();

	if( false == g_db_manager->initialize() )
		return; 


	g_server->init_npcs();

	g_server->initialize();

	g_db_manager->bind_server_completion_port(g_server->get_completion_port());

	g_server->start();

	g_server->finalize();

	//if (false == DbHelper::initialize(global::db_connection_string, global::concurrent_db_reader_thread_count))
	//	return;


	//c2::local::db_thread_id = 0;

	//{
	//	DbHelper db_helper;
	//	int id, level, hp, exp, y, x;
	//	char name[50];

	//	int uid = 100;
	//	wchar_t password[100];
	//	db_helper.bind_param_str("actor_1");

	//	db_helper.bind_result_column_int(&id);
	//	db_helper.bind_result_column_str(name, count_of(name));
	//	db_helper.bind_result_column_int(&y);
	//	db_helper.bind_result_column_int(&x);
	//	db_helper.bind_result_column_int(&level);
	//	db_helper.bind_result_column_int(&exp);
	//	db_helper.bind_result_column_int(&hp);
	//	
	//	if (db_helper.execute(sql_load_actor))
	//	{
	//		if (true == db_helper.fetch_row())
	//		{
	//			wprintf(L"%d %d %d %d %d, %d| \n", id,  level, y, x, hp, exp);
	//		}
	//	}
	//}


	//{
	//	DbHelper db_helper;
	//	int id, level, hp, exp, y, x;
	//	char name[100];

	//	int uid = 101;
	//	db_helper.bind_param_str("actor_5");

	//	db_helper.bind_result_column_int(&id);
	//	db_helper.bind_result_column_str(name, count_of(name));
	//	db_helper.bind_result_column_int(&y);
	//	db_helper.bind_result_column_int(&x);
	//	db_helper.bind_result_column_int(&level);
	//	db_helper.bind_result_column_int(&exp);
	//	db_helper.bind_result_column_int(&hp);

	//	if (db_helper.execute(sql_load_actor))
	//	{
	//		if (true == db_helper.fetch_row())
	//		{
	//			wprintf(L"%d %d %d %d %d, %d| \n", id, level, y, x, hp, exp);
	//		}
	//	}
	//}


	//{
	//	DbHelper db_helper;

	//	int uid = 101, y = 259, x = 144;
	//	db_helper.bind_param_int(&uid);
	//	db_helper.bind_param_int(&y);
	//	db_helper.bind_param_int(&x);
	//	if (true == db_helper.execute(sql_update_actor_position))
	//	{
	//		if (true == db_helper.fetch_row())
	//		{
	//			wprintf(L"actor position update ok\n");
	//		}
	//	}
	//}

	//{
	//	DbHelper db_helper;
	//	const char* name{ "actor_21" };
	//	wchar_t password[100];

	//	db_helper.bind_param_str(name);
	//	if (true == db_helper.execute(sql_create_actor))
	//	{
	//		if (true == db_helper.fetch_row())
	//		{
	//			wprintf(L"actor creation ok\n");
	//		}
	//	}
	//}




}