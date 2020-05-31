#include "../C2Server/C2Server/pre_compile.h"
#include "MMOSession.h"
#include "MMOSimulator.h"
#include "MMOZone.h"
#include "MMOActor.h"
#include "MMOServer.h"

MMOActor::MMOActor(MMOSession* owner)
	: x{}, y{},
	name{},
	session_id{},
	//current_sector{},//, prev_sector{},
	zone{}, 
	session{ owner }, last_move_time{}
{
	InitializeSRWLock(&lock);
}

MMOActor::~MMOActor(){}

void MMOActor::enter_sector(int32_t x, int32_t y)
{
	//prev_sector = nullptr;

	//current_sector/* = &zone->sectors[x][y];*/

	
}



void MMOActor::move(int8_t direction)
{



}

void MMOActor::reset()
{
	session_id = this->session->session_id;
	zone = nullptr;
	current_exp = 0;
	levelup_exp = 0;
	hp = 10;
	level = 1;
	sector_x =  sector_y = 0;
	direction = NEAR_MAX;

	static int g_seed = 2;

	g_seed = (214013 * g_seed + 2531011);
	size_t ret = ((g_seed >> 16) & 0x7FFF);

	x = ret % c2::constant::MAP_WIDTH;

	g_seed = (214013 * g_seed + 2531011);
	ret = ((g_seed >> 16) & 0x7FFF);

	y = ret % c2::constant::MAP_HEIGHT;
	

	//x = n;
	//y = n++;


	name[0] = NULL;

	//current_sector	= nullptr;

	AcquireSRWLockExclusive(&lock);
	view_list.clear();
	ReleaseSRWLockExclusive(&lock);
}

void MMOActor::exit()
{
}

bool MMOActor::is_near(MMOActor* other)
{
	if (abs(this->x - other->x) > FOV_HALF_WIDTH) return false;
	if (abs(this->y - other->y) > FOV_HALF_HEIGHT) return false;

	return true;
}




void MMOActor::get_login_packet_info(sc_packet_login_ok& out_payload)
{
	out_payload.header.length = sizeof(sc_packet_login_ok);
	out_payload.header.type = c2::enumeration::S2C_LOGIN_OK;

	out_payload.id = (int16_t)this->session_id;

	out_payload.x = this->x;
	out_payload.y = this->y;
	
	out_payload.hp = this->hp;
	out_payload.exp = this->current_exp;
	out_payload.level = this->level;
}

int16_t MMOActor::get_id()
{
	return int16_t(session_id);
}

// 내 뷰리스트에 상대를 추가하고, 정보도 보냄. 
void MMOActor::send_enter_packet(MMOActor* other)
{
	sc_packet_enter payload;
	payload.id = other->get_id();
	payload.header.length = sizeof(sc_packet_enter);
	payload.header.type = S2C_ENTER;
	payload.x = other->x;
	payload.y = other->y;
	strcpy_s(payload.name, other->name);
	payload.o_type = 0;

	AcquireSRWLockExclusive(&this->lock);
	this->view_list.emplace(other->get_id(), other);
	ReleaseSRWLockExclusive(&this->lock);

	c2::Packet* enter_packet = c2::Packet::alloc();
	enter_packet->write(&payload, sizeof(sc_packet_enter));

	server->send_packet(this->session_id, enter_packet);

	//printf("send_enter_packet() my id : %llu  other id : %llu  \n", this->session_id, payload.id);
}

void MMOActor::send_login_ok_packet()
{
	sc_packet_login_ok p;

	this->get_login_packet_info(p);

	c2::Packet* login_ok_packet = c2::Packet::alloc();

	login_ok_packet->write(&p, sizeof(sc_packet_login_ok));

	server->send_packet(this->session_id, login_ok_packet);
	//printf("send_login_ok_packet() id : %llu \n", this->session_id);
}

void MMOActor::send_move_packet(MMOActor* other)
{
	sc_packet_move payload;
	payload.header.length = sizeof(sc_packet_move);
	payload.header.type = S2C_MOVE;

	payload.id			= other->get_id();
	payload.x			= other->x;
	payload.y			= other->y;
	payload.move_time	= other->last_move_time;
	
	c2::Packet* move_packet =  c2::Packet::alloc();

	move_packet->write(&payload, sizeof(sc_packet_move));

	server->send_packet( this->session_id, move_packet);

	//printf("send_move_packet() my id : %llu  other id : %llu  \n", this->session_id, payload.id);
}

void MMOActor::send_leave_packet(MMOActor* other)
{
	sc_packet_leave payload;
	payload.id = other->get_id();
	payload.header.length = sizeof(sc_packet_leave);
	payload.header.type = S2C_LEAVE;


	AcquireSRWLockExclusive(&this->lock);
	this->view_list.erase(other->get_id());
	ReleaseSRWLockExclusive(&this->lock);


	c2::Packet* leave_packet = c2::Packet::alloc();
	leave_packet->write(&payload, sizeof(sc_packet_leave));

	server->send_packet(this->session_id, leave_packet);

	//printf("send_leave_packet() my id : %llu  other id : %llu  \n", this->session_id, payload.id);
}

void MMOActor::sned_chat_packet(MMOActor* other)
{

}


void MMOActor::attack()
{
	// 주변 범위를 공격함.
}


