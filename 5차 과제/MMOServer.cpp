//#include "../C2Server/C2Server/pre_compile.h"


#include "main.h"
#include "core/OuterServer.h"
#include "MMOServer.h"
#include "MMONpcManager.h"
#include "core/util/TimeScheduler.h"
#include "MMOZone.h"


MMOServer::MMOServer() : OuterServer{}, zone{}
{
	zone = new MMOZone{};
}

MMOServer::~MMOServer()
{
}

void MMOServer::init_npcs()
{
	g_npc_manager = new MMONpcManager();
	g_npc_manager->set_zone(zone);
	g_npc_manager->initilize();
	g_npc_manager->place_npc_in_zone();

	printf("NPC 초기화 완료.\n");
}

void MMOServer::on_create_sessions(size_t capacity)
{
	session_mapping_helper(MMOSession);
}

void MMOServer::on_connect(uint64_t session_id){}
void MMOServer::on_disconnect(uint64_t session_id) 
{
	MMOActor*	my_actor = get_actor(session_id);
	int			my_actor_id = my_actor->get_id();
	MMOSector*	my_actor_sector = my_actor->current_sector;
	AcquireSRWLockExclusive(&my_actor->lock);
	
	AcquireSRWLockExclusive(&my_actor_sector->lock);
	my_actor_sector->actors.erase(my_actor_id);
	ReleaseSRWLockExclusive(&my_actor_sector->lock);

	auto& view_list = my_actor->view_list;
	for( auto& actor_iter : view_list)
	{
		//if (actor_iter.second == my_actor) continue;
		AcquireSRWLockExclusive(&actor_iter.second->lock); // 데드락 가능성 있다;
		if (0 != actor_iter.second->view_list.count(my_actor_id))
		{
			ReleaseSRWLockExclusive(&actor_iter.second->lock);
			actor_iter.second->send_leave_packet(my_actor);
		}
		else
		{
			ReleaseSRWLockExclusive(&actor_iter.second->lock);
		}
	}
	view_list.clear();

	ReleaseSRWLockExclusive(&my_actor->lock);
}
bool MMOServer::on_accept(Session* session) 
{ 
	//printf( "accept() : %d \n", session->session_id );
	return true; 
}
void MMOServer::on_wake_io_thread(){}
void MMOServer::on_sleep_io_thread(){}
void MMOServer::custom_precedure(uint64_t idx) {}
void MMOServer::on_update()
{

}

//#include "../C2Server/C2Server/IO/KeyManager.h"
void MMOServer::on_start()
{
	//KeyManager km;

	for (;;)
	{
		//if (km.key_down(VK_RETURN))
		//{
			//printf("hi\n");
		//}

		Sleep(30);
	}

	return;
}

//
void MMOServer::on_timer_service(const TimerTask& task)
{
	switch (task.task_type)
	{
	case TTT_MOVE_NPC:
	{
		MMONpc* npc = g_npc_manager->get_npc(task.server_id);

		npc->move();
		
		break;
	}


	default:
		size_t* invliad_ptr{}; *invliad_ptr = 0;
	}
}

void MMOServer::create_npcs(size_t capacity)
{

}

MMOActor* MMOServer::get_actor(uint64_t session_id)
{
	return ((MMOSession*)sessions[(uint16_t)session_id])->get_actor();
}

MMOZone* MMOServer::get_zone()
{
	return zone;
}

