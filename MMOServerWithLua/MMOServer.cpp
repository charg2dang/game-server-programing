//#include "pre_compile.h"


#include "main.h"
#include "OuterServer.h"
#include "MMOServer.h"
#include "MMONpcManager.h"
#include "util/TimeScheduler.h"
#include "MMOZone.h"


MMOServer::MMOServer() : OuterServer{}, zone{}
{
	zone = new MMOZone{};
	zone->server = this;
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
	if (my_actor_sector == nullptr)
	{
		return;
	}
	AcquireSRWLockExclusive(&my_actor->lock);
	
	AcquireSRWLockExclusive(&my_actor_sector->lock);
	my_actor_sector->actors.erase(my_actor_id);
	ReleaseSRWLockExclusive(&my_actor_sector->lock);

	auto& view_list = my_actor->view_list;
	for( auto& actor_iter : view_list)
	{
		//if (actor_iter.second == my_actor) continue;
		AcquireSRWLockExclusive(&actor_iter.second->lock); // ����� ���ɼ� �ִ�;
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

//#include "IO/KeyManager.h"
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
		case TTT_NPC_SCRIPT:
		{
			MMONpc*		npc		= g_npc_manager->get_npc(task.actor_id);
			MMOActor*	actor	= this->get_actor(task.target_id);
			lua_State*	vm		= npc->lua_vm;
			
			AcquireSRWLockExclusive(&npc->vm_lock); // �������� �����ؾ���.
			
			lua_getglobal(vm, "event_palayer_move"); // 
			lua_pushnumber(vm, actor->session_id);
			lua_pushnumber(vm, actor->x);
			lua_pushnumber(vm, actor->y);

			lua_pcall(vm, 3, 0, 0);
			
			ReleaseSRWLockExclusive(&npc->vm_lock);

			break;
		}

		case TTT_NPC_SCRIPT2: // �Ѹ��� ����.
		{
			MMONpc*		npc		= g_npc_manager->get_npc(task.actor_id);
			//MMOActor*	actor	= this->get_actor(task.target_id);
			lua_State*  vm		= npc->lua_vm;

			npc->move_to_anywhere();

			break;
		}

		default:
		{
			size_t* invliad_ptr{}; *invliad_ptr = 0;
		}
	}
}

bool MMOServer::on_load_config(c2::util::JsonParser* parser)
{
	uint32_t max_npc{};
	if (false == parser->get_uint32(L"maximum_npc_count", max_npc))
		return false;
	MMONpcManager::set_max_npc_count(max_npc);



	return true;
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
