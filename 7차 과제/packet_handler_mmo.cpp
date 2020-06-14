#include "core/packet_handler.h"
//#include "core/OuterServer.h"
#include "MMOSession.h"
#include "MMOServer.h"
#include "MMOZone.h"
#include "MMONpcManager.h"
#include "DBManager.h"

#include <unordered_map>
#include <unordered_set>

REGISTER_HANDLER(C2S_LOGIN)
{
	MMOSession* mmo_session		= (MMOSession*)session;
	MMOActor*	my_actor		= mmo_session->get_actor();
	MMOServer*	mmo_server		= (MMOServer*)session->server;
	my_actor->zone				= mmo_server->get_zone();
	my_actor->server			= mmo_server;
	MMOZone*	mmo_zone		= (MMOZone*)my_actor->zone;
	MMONpcManager* mmo_npc_mgr	= g_npc_manager;

////////////////////////////////// 들어온 정보로 클라이언트 업데이트.
	cs_packet_login login_payload;
	in_packet.read(&login_payload, sizeof(cs_packet_login));
	
	my_actor->reset();
	memcpy(my_actor->name, login_payload.name, c2::constant::MAX_ID_LEN); // 어차피 여기서만 write 함. and 나갈 때;;

	if (mmo_session->get_actor()->get_id() != (uint16_t)session->session_id) // 이미 나갔다 들어온 녀석.
	{
		printf("Session::invalid session id %d\n", my_actor->session_id );

		mmo_session->request_disconnection();

		return;
	}

	mmo_session->request_login_validation(login_payload.name);

	return;
}



REGISTER_HANDLER(C2S_MOVE)
{
	MMOSession* mmo_session = (MMOSession*)session;
	MMOActor*	my_actor	= mmo_session->get_actor();
	MMOServer*	mmo_server	= (MMOServer*)session->server;
	MMOZone*	mmo_zone	= mmo_server->get_zone();

	MMONpcManager* mmo_npc_mgr = g_npc_manager;

	cs_packet_move cs_move_payload;								//	들어온 이동정보.
	in_packet.read(&cs_move_payload, sizeof(cs_packet_move));	// 


	// 사본 만들기.
	int local_y = my_actor->y;
	int local_x = my_actor->x;
	int local_actor_id = my_actor->get_id();

	
	// 장애물 체크 등등.
	switch (cs_move_payload.direction)
	{
	case c2::constant::D_DOWN: 
		if (local_y < MAP_HEIGHT -1)	local_y++; 
		break;
	case c2::constant::D_LEFT:
		if (local_x > 0) local_x--;
		break;
	case c2::constant::D_RIGHT:
		if (local_x < MAP_WIDTH - 1) local_x++;
		break;
	case c2::constant::D_UP:
		if (local_y > 0) local_y--;
		break;
	default:
		size_t* invalid_ptr{}; *invalid_ptr = 0;
		break;
	}


	my_actor->x = local_x;
	my_actor->y = local_y;

	mmo_session->request_updating_position(local_y, local_x);

	MMOSector* prev_sector = my_actor->current_sector;					// view_list 긁어오기.
	MMOSector* curent_sector = mmo_zone->get_sector(local_y, local_x);			// view_list 긁어오기.


	if (prev_sector != curent_sector)										//섹터가 바뀐 경우.
	{
		AcquireSRWLockExclusive(&curent_sector->lock);						// 이전 섹터에서 나가기 위해서.
		prev_sector->actors.erase(local_actor_id);
		ReleaseSRWLockExclusive(&curent_sector->lock);						

		AcquireSRWLockExclusive(&curent_sector->lock);						// 내 view_list 에 접근하기 쓰기 위해서 락을 얻고 
		curent_sector->actors.emplace(local_actor_id, my_actor);
		my_actor->current_sector = curent_sector;							// 타 스레드에서 접근하면 여기 일로 하고?
		ReleaseSRWLockExclusive(&curent_sector->lock);						// 내 view_list 에 접근하기 쓰기 위해서 락을 얻고 
		
		//my_actor->session->request_updating_position(my_actor->y, my_actor->x);
	}






	AcquireSRWLockShared(&my_actor->lock);
	std::unordered_map<int32_t, MMOActor*> local_old_view_list = my_actor->view_list;
	std::unordered_set<int32_t> local_old_view_list_for_npc = my_actor->view_list_for_npc;
	ReleaseSRWLockShared(&my_actor->lock);



	/// 섹터 구하기...
	MMOSector*		current_sector	= curent_sector;
	const MMONear*	nears			= current_sector->get_near(local_y, local_x);
	int				near_cnt		= nears->count;


	// 내 주변 정보를 긁어 모음.
	std::unordered_map<int32_t, MMOActor*>	local_new_view_list;
	std::unordered_set<int32_t>				local_new_view_list_for_npc;
	for (int n = 0; n < near_cnt; ++n)					
	{
		AcquireSRWLockShared(&nears->sectors[n]->lock); // sector에 읽기 위해서 락을 얻고 
		for (auto& actor_iter : nears->sectors[n]->actors)
		{
			//if (actor_iter.second->status != ST_ACTIVE) 
				//continue;
			if (actor_iter.second == my_actor)
				continue;

			if (my_actor->is_near(actor_iter.second) == true) // 내 근처가 맞다면 넣음.
				local_new_view_list.insert(actor_iter);
		}

		for (auto npc_id : nears->sectors[n]->npcs)
		{
			//if (actor_iter.second->status != ST_ACTIVE) 
				//continue;
			MMONpc* npc = mmo_npc_mgr->get_npc(npc_id);
			if (my_actor->is_near(npc) == true) // 내 근처가 맞다면 넣음.
			{
				//my_actor->wake_up_npc(npc);
				local_timer->push_timer_task(npc->id, TTT_NPC_SCRIPT, 1, my_actor->session_id);

				local_new_view_list_for_npc.insert(npc_id);
			}
		}
		ReleaseSRWLockShared(&nears->sectors[n]->lock);
	}
	
	
	
	c2::Packet*			my_move_packet = c2::Packet::alloc();
	sc_packet_move		sc_move_payload{ {sizeof(sc_packet_move), S2C_MOVE}, local_actor_id, local_x, local_y, cs_move_payload.move_time };

	my_move_packet->write(&sc_move_payload, sizeof(sc_packet_move));  // 나한테 내 이동전송.
	mmo_server->send_packet(my_actor->session_id, my_move_packet);


///////////////////
	for (auto& new_actor : local_new_view_list)
	{
		if( 0 == local_old_view_list.count(new_actor.first) ) // 이동후 새로 보이는 유저.
		{
			my_actor->send_enter_packet(new_actor.second);		// 타인 정보를 나한테 보냄.

			AcquireSRWLockShared(&new_actor.second->lock);							
			if ( 0 == new_actor.second->view_list.count(my_actor->get_id()) ) // 타 스레드에서 시야 처리 안 된경우.
			{
				ReleaseSRWLockShared(&new_actor.second->lock);

				new_actor.second->send_enter_packet(my_actor);
			}
			else // 처리 된경우
			{
				ReleaseSRWLockShared(&new_actor.second->lock);

				new_actor.second->send_move_packet(my_actor);   // 상대 시야 리스트에 내가 있는 경우 뷰리스트만 업데이트 한다.
			}
		}
		else  // 기존 뷰리스트에 있던 유저들 
		{
			AcquireSRWLockShared(&new_actor.second->lock);					//
			if (0 != new_actor.second->view_list.count(my_actor->get_id()))	// 
			{
				ReleaseSRWLockShared(&new_actor.second->lock);

				new_actor.second->send_move_packet(my_actor);
			}
			else	// 이미 나간 경우.
			{
				ReleaseSRWLockShared(&new_actor.second->lock);

				new_actor.second->send_enter_packet(my_actor);
			}
		}
	}

	//시야에서 벗어난 플레이어
	for (auto& old_it : local_old_view_list)
	{
		if (0 == local_new_view_list.count(old_it.first))
		{
			my_actor->send_leave_packet(old_it.second);

			AcquireSRWLockShared(&old_it.second->lock);
			if (0 != old_it.second->view_list.count(my_actor->get_id()))
			{
				ReleaseSRWLockShared(&old_it.second->lock);
				old_it.second->send_leave_packet(my_actor);
			}
			else	// 이미 다른 스레드에서 지워준 경우.
			{
				ReleaseSRWLockShared(&old_it.second->lock);
			}
		}
	}


// 유저가 npc한테 하는 동작 추가.
	///////////////////
	for (int32_t npc_id : local_new_view_list_for_npc)
	{
		if (0 == local_old_view_list_for_npc.count(npc_id)) // 이동후 새로 보이는 NPC
		{
			MMONpc* npc = mmo_npc_mgr->get_npc(npc_id);
			
			my_actor->send_enter_packet(npc); // 이 npc 정보를 나한테 보냄.

		}
	}

	for (int32_t old_npc_id : local_old_view_list_for_npc)
	{
		if (0 == local_new_view_list_for_npc.count(old_npc_id))
		{
			//AcquireSRWLockExclusive(&my_actor->lock);
			//my_actor->view_list_for_npc.erase(old_npc_id);
			//ReleaseSRWLockExclusive(&my_actor->lock);
			// 떠나는건 npc가 알아서 해줌. // 얘는 1초에 한번이기도 하고 내가 반영을 안함.
			MMONpc* npc = mmo_npc_mgr->get_npc(old_npc_id);

			my_actor->send_leave_packet(npc); // 이 npc 정보를 나한테 보냄.
		}
	}

}


REGISTER_HANDLER(C2S_CHAT)
{
	MMOSession* mmo_session		{ (MMOSession*)session };
	MMOActor*	my_actor		{ mmo_session->get_actor() };
	MMOServer*	mmo_server		{ (MMOServer*)session->server };
	MMOSector*	sector			{ my_actor->current_sector };

	cs_packet_chat chat_payload; 								// id check
	in_packet.read(&chat_payload, sizeof(cs_packet_chat));


	//static thread_local 

	std::unordered_map<int16_t, MMOActor*> local_view_list;
	local_view_list.clear();


	MMOSector* current_sector = mmo_server->get_zone()->get_sector(my_actor);			// view_list 긁어오기.
	const MMONear* nears = current_sector->get_near(my_actor->y, my_actor->x); // 주벽 섹터들.
	int near_cnt = nears->count;

	for (int i = 0; i < near_cnt; ++i)
	{
		AcquireSRWLockShared(&nears->sectors[i]->lock);

		for (auto& iter : nears->sectors[i]->actors)
		{
			MMOActor* neighbor = iter.second;
			if (my_actor->is_near(neighbor) == true) // 내 근처가 맞다면 넣음.
				local_view_list.emplace(iter);
		}
		
		ReleaseSRWLockShared(&nears->sectors[i]->lock);
	}
	

	c2::Packet* out_packet = c2::Packet::alloc();
	// 프로토콜이 형식이 똑같음 타입만 바꿔주면 됨.
	chat_payload.header.type = S2C_CHAT;
	out_packet->write(&chat_payload, sizeof(cs_packet_chat));	//여기가 좀 병목인가;
	out_packet->add_ref( local_view_list.size() );
	for (auto& it : local_view_list)
	{
		mmo_server->send_packet(it.second->session_id, out_packet);	// 
	}

	out_packet->decrease_ref_count();
}

