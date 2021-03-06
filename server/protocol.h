#pragma once

#include <cstdint>
//#include "enviroment.h"
#include "contents_enviroment.h"
//#include "../../EchoServer/mmo_function.hpp"


namespace c2::enumeration
{
	//enum PacketType : char
	//{
	//	PT_NONE,

	//	C2S_LOGIN = 1,
	//	C2S_MOVE,
	//	C2S_CHAT,

	//	S2C_LOGIN_OK = 1,
	//	S2C_MOVE,
	//	S2C_ENTER,
	//	S2C_LEAVE,
	//	S2C_CHAT,

	//	PT_MAX,
	//};


	enum PacketType : unsigned char
	{
		PT_NONE,

		C2S_LOGIN = 1,
		C2S_MOVE,
		C2S_ATTACK,
		C2S_CHAT,
		C2S_LOGOUT,

		S2C_LOGIN_OK = 1,
		S2C_LOGIN_FAIL,
		S2C_MOVE,
		S2C_ENTER,
		S2C_LEAVE,
		S2C_CHAT,
		S2C_STAT_CHANGE,
		PT_MAX,

	};


	
}



#pragma pack(push, 1)

using namespace c2::enumeration;
using namespace c2::constant;

struct PacketHeader
{
	unsigned char	length;
	PacketType		type;

	static constexpr size_t header_length{ sizeof(uint16_t) + sizeof(PacketType) };
};

static_assert(sizeof(int32_t) == sizeof(int) && sizeof(uint32_t) == sizeof(unsigned int));

struct sc_packet_login_ok // 2 4 4 2 2 4  = 18
{
	PacketHeader header;

	int		id;
	short	x, y;
	short	hp;
	short	level;
	int		exp;
};

struct sc_packet_login_fail 
{
	PacketHeader header;
};


struct sc_packet_move
{
	PacketHeader header;

	int id;
	short x, y;
	unsigned move_time;
};

struct sc_packet_enter // 2 + 4 + 100 + 1 + 4
{ 
	PacketHeader header;

	int id;
	char o_type;
	short x, y;

	wchar_t name[MAX_ID_LEN];
};

struct sc_packet_leave
{
	PacketHeader header;

	int id;
};

struct sc_packet_chat
{
	PacketHeader header;

	int	 id;
	wchar_t chat[MAX_CHAT_LEN];
};

struct sc_packet_stat_change 
{
	PacketHeader header;

	short hp;
	short level;
	int	exp;
};

struct cs_packet_login
{
	PacketHeader header;

	wchar_t 	name[MAX_ID_LEN];
};


struct cs_packet_move 
{
	PacketHeader header;

	char		direction;
	unsigned	move_time;
};

struct cs_packet_attack 
{
	PacketHeader header;
};

struct cs_packet_chat
{
	PacketHeader header;

	int		id;
	wchar_t chat[MAX_CHAT_LEN];
};

struct cs_packet_logout 
{
	PacketHeader header;
};

#pragma pack(pop)
