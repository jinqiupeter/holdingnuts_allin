/*
 * Copyright 2008, 2009, Dominik Geyer
 *
 * This file is part of HoldingNuts.
 *
 * HoldingNuts is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HoldingNuts is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HoldingNuts.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *     Dominik Geyer <dominik.geyer@holdingnuts.net>
 */


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>

#include "Config.h"
#include "Platform.h"
#include "Network.h"
#include "Debug.h"
#include "Logger.h"
#include "Tokenizer.hpp"
#include "ConfigParser.hpp"

#include "game.hpp"
#include "ranking.hpp"
#include <sstream>


using namespace std;

extern ConfigParser config;

// temporary buffer for sending messages
#define MSG_BUFFER_SIZE  (1024*16)
static char msg[MSG_BUFFER_SIZE];

static games_type games;

static clients_type clients;


static clientconar_type con_archive;
static time_t last_conarchive_cleanup = 0;   // last time scan

static server_stats stats;



GameController* get_game_by_id(int gid)
{
	games_type::const_iterator it = games.find(gid);
	if (it != games.end())
		return it->second;
	else
		return NULL;
}

// for pserver.cpp filling FD_SET
clients_type& get_client_vector()
{
	return clients;
}

clientcon* get_client_by_sock(socktype sock)
{
	for (unsigned int i=0; i < clients.size(); i++)
		if (clients[i].sock == sock)
			return &(clients[i]);
	
	return NULL;
}

clientcon* get_client_by_id(int cid)
{
	for (unsigned int i=0; i < clients.size(); i++)
		if (clients[i].id == cid)
			return &(clients[i]);
	
	return NULL;
}

int send_msg(socktype sock, const char *message)
{
	char buf[MSG_BUFFER_SIZE];
	const int len = snprintf(buf, sizeof(buf), "%s\r\n", message);
	const int bytes = socket_write(sock, buf, len);
	
	// FIXME: send remaining bytes if not all have been sent
	// FIXME: handle client-dead case
	if (len != bytes)
		dbg_msg("clientsock", "message %s: (%d) warning: not all bytes written (%d != %d).",
			message, sock, len, bytes);
	
	return bytes;
}

bool send_response(socktype sock, bool is_success, int last_msgid, int code=0, const char *str="")
{
	char buf[512];
	if (last_msgid == -1)
		snprintf(buf, sizeof(buf), "%s %d %s",
			is_success ? "OK" : "ERR", code, str);
	else
		snprintf(buf, sizeof(buf), "%d %s %d %s",
			  last_msgid, is_success ? "OK" : "ERR", code, str);
	
	return send_msg(sock, buf);
}

bool send_ok(clientcon *client, int code=0, const char *str="")
{
	return send_response(client->sock, true, client->last_msgid, code, str);
}

bool send_err(clientcon *client, int code=0, const char *str="")
{
	return send_response(client->sock, false, client->last_msgid, code, str);
}

// from client/foyer to client/foyer
bool client_chat(int from, int to, const char *message)
{
	char msg[256];
	
	if (from == -1)
	{
		snprintf(msg, sizeof(msg), "MSG %d %s %s",
			from, "foyer", message);
	}
	else
	{
		clientcon* fromclient = get_client_by_id(from);
		
		snprintf(msg, sizeof(msg), "MSG %d \"%s\" %s",
			from,
			(fromclient) ? fromclient->info.name : "???",
			message);
	}
	
	if (to == -1)
	{
		for (clients_type::iterator e = clients.begin(); e != clients.end(); e++)
		{
			if (!(e->state & Introduced))  // do not send broadcast to non-introduced clients
				continue;
			
			send_msg(e->sock, msg);
		}
	}
	else
	{
		clientcon* toclient = get_client_by_id(to);
		if (toclient)
			send_msg(toclient->sock, msg);
		else
			return false;
	}
	
	return true;
}

// from game/table to client
bool client_chat(int from_gid, int from_tid, int to, const char *message)
{
	char msg[256];
	
	snprintf(msg, sizeof(msg), "MSG %d:%d %s %s",
		from_gid, from_tid, (from_tid == -1) ? "game" : "table", message);
	
	clientcon* toclient = get_client_by_id(to);
	if (toclient)
		send_msg(toclient->sock, msg);
	
	return true;
}

// from client to game/table
bool table_chat(int from_cid, int to_gid, int to_tid, const char *message)
{
	char msg[256];
	
	clientcon* fromclient = get_client_by_id(from_cid);
	
	GameController *g = get_game_by_id(to_gid);
	if (!g)
		return false;
	
	vector<int> client_list;
	g->getListenerList(client_list);
	
	for (unsigned int i=0; i < client_list.size(); i++)
	{
		snprintf(msg, sizeof(msg), "MSG %d:%d:%d \"%s\" %s",
			to_gid, to_tid, from_cid,
			(fromclient) ? fromclient->info.name : "???",
			message);
		
		clientcon* toclient = get_client_by_id(client_list[i]);
		if (toclient)
			send_msg(toclient->sock, msg);
	}
	
	return true;
}

bool client_snapshot(int from_gid, int from_tid, int to, int sid, const char *message)
{
	char buf[MSG_BUFFER_SIZE];
	snprintf(buf, sizeof(buf), "SNAP %d:%d %d %s",
		from_gid, from_tid, sid, message);
	
	clientcon* toclient = get_client_by_id(to);
	if (toclient && toclient->state & Introduced) {
		send_msg(toclient->sock, buf);
    }
	
	return true;
}

bool client_snapshot(int to, int sid, const char *message)
{
	if (to == -1)  // to all
	{
		for (clients_type::iterator e = clients.begin(); e != clients.end(); e++)
			client_snapshot(-1, -1, e->id, sid, message);
	}
	else
		client_snapshot(-1, -1, to, sid, message);
	
	return true;
}

bool send_ok_game(int gid, clientcon *client)
{
    stringstream s;
    s << gid;
    string sgid(s.str());
    send_ok(client, 0, sgid.c_str());

    return true;
}

bool client_add(socktype sock, sockaddr_in *saddr)
{
	// add the client
	clientcon client;
	memset(&client, 0, sizeof(client));
	client.sock = sock;
	client.saddr = *saddr;
	client.id = -1;
	
	// set initial state
	client.state |= Connected;
	
	clients.push_back(client);
	
	
	// update stats
	stats.clients_connected++;
	
	return true;
}

bool client_remove(socktype sock)
{
	for (clients_type::iterator client = clients.begin(); client != clients.end(); client++)
	{
		if (client->sock == sock)
		{
			socket_close(client->sock);
			
			bool send_msg = false;
			if (client->state & SentInfo)
			{
				// remove player from unstarted games
				for (games_type::iterator e = games.begin(); e != games.end(); e++)
				{
					GameController *g = e->second;
					if (!g->isStarted() && g->isPlayer(client->id))
						g->removePlayer(client->id);
				}
				
				
				snprintf(msg, sizeof(msg),
					"%d %d \"%s\"",
					SnapFoyerLeave, client->id, client->info.name);
				
				send_msg = true;
				
				// save client-con in archive
				string uuid = client->uuid;
				
				if (uuid.length())
				{
					// FIXME: only add max. 3 entries for each IP
					con_archive[uuid].logout_time = time(NULL);
				}
			}
			
			log_msg("clientsock", "(%d) connection closed", client->sock);
			
			clients.erase(client);
			
			// send foyer snapshot to all remaining clients
			if (send_msg)
				client_snapshot(-1, SnapFoyer, msg);
			
			break;
		}
	}
	
	return true;
}

int client_cmd_pclient(clientcon *client, Tokenizer &t)
{
	unsigned int version = t.getNextInt();
	string uuid = t.getNext();
    unsigned int cid = t.getNextInt();
	
	if (version < VERSION_COMPAT)
	{
		log_msg("client", "client %d version (%d) too old", client->sock, version);
		send_err(client, ErrWrongVersion, "The client version is too old."
			"Please update your HoldingNuts client to a more recent version.");
		client_remove(client->sock);
		
		
		// update stats
		stats.clients_incompatible++;
	}
	else
	{
		// ack the command
		send_ok(client);
		
		
		client->version = version;
		client->state |= Introduced;
		
		// update stats
		stats.clients_introduced++;
		
		
		snprintf(client->uuid, sizeof(client->uuid), "%s", uuid.c_str());
		
		// re-assign cid if this client was previously connected (and cid isn't already connected)
		bool use_prev_cid = false;
		bool uuid_inuse = false;
		
		if (uuid.length())
		{
			clientconar_type::iterator it = con_archive.find(uuid);
			
			if (it != con_archive.end())
			{
				clientcon *conc = get_client_by_id(it->second.id);
				if (!conc)
				{
					client->id = it->second.id;
					use_prev_cid = true;
					
					log_msg("uuid", "(%d) using previous cid (%d) for uuid '%s'", client->sock, client->id, client->uuid);
				}
				else
				{
					log_msg("uuid", "(%d) uuid '%s' already connected; used by cid %d", client->sock, client->uuid, conc->id);
					client->uuid[0] = '\0';    // client is not allowed to use this uuid
					uuid_inuse = true;
				}
			}
			else
				log_msg("uuid", "(%d) reserving uuid '%s'", client->sock, client->uuid);
		}
		
		if (!use_prev_cid) {
			client->id = cid;
        }
		
		
		// set initial client info
		snprintf(client->info.name, sizeof(client->info.name), "client_%d", client->id);
		*(client->info.location) = '\0';
		
		// send 'introduced response'
		snprintf(msg, sizeof(msg), "PSERVER %d %d %d",
			VERSION,
			client->id,
			(unsigned int) time(NULL));
			
		send_msg(client->sock, msg);
		
		
		// send warning if UUID is already in use
		if (uuid_inuse)
			client_chat(-1, client->id, "Warning: UUID is already in use.");
	}
	
	return 0;
}

int client_cmd_info(clientcon *client, Tokenizer &t)
{
	string infostr;
	Tokenizer it(":");
	
	while (t.getNext(infostr))
	{
		it.parse(infostr);
		
		string infotype, infoarg;
		it.getNext(infotype);
		
		bool havearg = it.getNext(infoarg);
		
		if (infotype == "name" && havearg)
		{
			// allow name-change only once per session
			if (!(client->state & SentInfo))
				snprintf(client->info.name, sizeof(client->info.name), "%s", infoarg.c_str());
		}
		else if (infotype == "location" && havearg)
			snprintf(client->info.location, sizeof(client->info.location), "%s", infoarg.c_str());
	}
	
	send_ok(client);
	
	if (!(client->state & SentInfo))
	{
		// store UUID in connection-archive
		if (*client->uuid)
		{
			clientcon_archive ar;
			memset(&ar, 0, sizeof(ar));
			ar.id = client->id;
			con_archive[client->uuid] = ar;
		}
		
		
		// send welcome message
		const string welcome_message = config.get("welcome_message");
		if (welcome_message.length())
		{
			snprintf(msg, sizeof(msg),
				"%s",
				welcome_message.c_str());
		
			client_chat(-1, client->id, msg);
		}
		
		
		// send foyer snapshot broadcast
		snprintf(msg, sizeof(msg),
			"%d %d \"%s\"",
			SnapFoyerJoin, client->id, client->info.name);
		
		client_snapshot(-1, SnapFoyer, msg);
	}
	
	client->state |= SentInfo;
	
	return 0;
}

int client_cmd_chat(clientcon *client, Tokenizer &t)
{
	bool cmderr = false;
	
	if (t.count() < 2)
		cmderr = true;
	else
	{
		// flooding-protection
		int time_since_last_chat = (int) difftime(time(NULL), client->last_chat);
		
		// is the client still muted?
		if (time_since_last_chat < 0)
		{
			send_err(client, 0, "you are still muted");
			return 0;
		}
		
		if ((unsigned int)time_since_last_chat > (unsigned int) config.getInt("flood_chat_interval"))
		{
			// reset flood-measure for new interval
			client->last_chat = time(NULL);
			client->chat_count = 0;
		}
		
		// is client flooding?
		if (++client->chat_count >= (unsigned int) config.getInt("flood_chat_per_interval"))
		{
			log_msg("flooding", "client (%d) caught flooding the chat", client->id);
			
			// mute client for n-seconds
			client->last_chat = time(NULL) + config.getInt("flood_chat_mute");
			client->chat_count = 0;
			
			send_err(client, 0, "you have been muted for some time");
			return 0;
		}
		
		
		Tokenizer ct(":");
		ct.parse(t.getNext());
		string chatmsg = t.getTillEnd();
		
		if (ct.count() == 1) // cid
		{
			int dest = ct.getNextInt();
			
			if (!client_chat(client->id, dest, chatmsg.c_str()))
				cmderr = true;
		}
		else if (ct.count() == 2)  // gid:tid
		{
			int gid = ct.getNextInt();
			int tid = ct.getNextInt();
			
			if (!table_chat(client->id, gid, tid, chatmsg.c_str()))
				cmderr = true;
		}
	}
	
	if (!cmderr)
		send_ok(client);
	else
		send_err(client);
	
	return 0;
}


bool send_gameinfo(clientcon *client, int gid)
{
	const GameController *g;
	if (!(g = get_game_by_id(gid)))
		return false;
	
	int game_mode = 0;
	switch ((int)g->getGameType())
	{
	case GameController::SNG:
		game_mode = GameModeSNG;
		break;
	case GameController::FreezeOut:
		game_mode = GameModeFreezeOut;
		break;
	case GameController::RingGame:
		game_mode = GameModeRingGame;
		break;
	}
	
	int state = 0;
	if (g->isEnded())
		state = GameStateEnded;
	else if (g->isStarted())
		state = GameStateStarted;
	else if (g->isPaused())
        state = GameStatePaused;
    else
		state = GameStateWaiting;
	
	snprintf(msg, sizeof(msg),
		"GAMEINFO %d %d:%d:%d:%d:%d:%d:%d:%d %d:%d:%d:%d:%d:%d \"%s\"",
		gid,
		(int) GameTypeHoldem,
		game_mode,
		state,
		(g->isPlayer(client->id) ? GameInfoRegistered : 0) |
			(g->isSpectator(client->id) ? GameInfoSubscribed : 0) |
			(g->hasPassword() ? GameInfoPassword : 0) |
			(g->getOwner() == client->id ? GameInfoOwner : 0) |
			(g->getRestart() ? GameInfoRestart : 0),
		g->getPlayerMax(),
		g->getPlayerCount(),
		g->getPlayerTimeout(),
		g->getPlayerStakes(),
		g->getBlindsStart(),
		g->getBlindsFactor(),
		g->getBlindsTime(),
 		g->getAnte(),
		(g->getMandatoryStraddle() ? 1 : 0),
		(g->getEnableInsurance() ? 1 : 0),
        g->getName().c_str());
	
	send_msg(client->sock, msg);
	
	return true;
}

bool client_cmd_request_gameinfo(clientcon *client, Tokenizer &t)
{
	string sgid;
	while (t.getNext(sgid))   // FIXME: have maximum for count of requests
	{
		const int gid = Tokenizer::string2int(sgid);
		send_gameinfo(client, gid);
	}
	
	return true;
}

bool client_cmd_request_clientinfo(clientcon *client, Tokenizer &t)
{
	string scid;
	while (t.getNext(scid))   // FIXME: have maximum for count of requests
	{
		const socktype cid = Tokenizer::string2int(scid);
		const clientcon *c;
		if ((c = get_client_by_id(cid)))
		{
			snprintf(msg, sizeof(msg),
				"CLIENTINFO %d \"name:%s\" \"location:%s\"",
				cid,
				c->info.name, c->info.location);
			
			send_msg(client->sock, msg);
		}
	}
	
	return true;
}

bool client_cmd_request_gamelist(clientcon *client, Tokenizer &t)
{
	string gamelist;
	for (games_type::iterator e = games.begin(); e != games.end(); e++)
	{
		const int gid = e->first;
		
		snprintf(msg, sizeof(msg), "%d ", gid);
		gamelist += msg;
	}
	
	snprintf(msg, sizeof(msg),
		"GAMELIST %s", gamelist.c_str());
	
	send_msg(client->sock, msg);
	
	return true;
}

bool send_playerlist(int gid, clientcon *client)
{
	GameController *g = get_game_by_id(gid);
	if (!g)
		return false;
	
	vector<string> client_list;
	g->getPlayerList(client_list);
	
	string slist;
	for (unsigned int i=0; i < client_list.size(); i++)
	{
		slist += client_list[i];
	}
	
	snprintf(msg, sizeof(msg), "PLAYERLIST %d %s", gid, slist.c_str());
	send_msg(client->sock, msg);
	
	return true;
}

bool client_cmd_request_playerlist(clientcon *client, Tokenizer &t)
{
	int gid;
	t >> gid;
	
    return send_playerlist(gid, client);
}

bool client_cmd_request_serverinfo(clientcon *client, Tokenizer &t)
{
	snprintf(msg, sizeof(msg), "SERVERINFO "
		"%d:%d %d:%d %d:%d %d:%d %d:%d %d:%d %d:%d %d:%d",
		StatsServerStarted,		(unsigned int) stats.server_started,
		StatsClientsConnected,		(unsigned int) stats.clients_connected,
		StatsClientsIntroduced,		(unsigned int) stats.clients_introduced,
		StatsClientsIncompatible,	(unsigned int) stats.clients_incompatible,
		StatsGamesCreated,		(unsigned int) stats.games_created,
		StatsClientCount,		(unsigned int) clients.size(),
		StatsGamesCount,		(unsigned int) games.size(),
		StatsConarchiveCount,		(unsigned int) con_archive.size());
	
	send_msg(client->sock, msg);
	
	return true;
}

bool client_cmd_request_gamestart(clientcon *client, Tokenizer &t)
{
	int gid;
	t >> gid;
	
	GameController *g = get_game_by_id(gid);
	if (!g)
		return false;
	
	if (g->getOwner() != client->id && !(client->state & Authed))
		return false;
	
	g->start();
	
    send_ok_game(gid, client);
	return true;
}

bool client_cmd_request_gamerestart(clientcon *client, Tokenizer &t)
{
	int gid, restart;
	t >> gid >> restart;

	GameController *g = get_game_by_id(gid);
	if (!g)
		return false;

	if (!(client->state & Authed))
		return false;

	g->setRestart(restart);

	return true;
}

bool client_cmd_request_gamepause(clientcon *client, Tokenizer &t)
{
	int gid;
	t >> gid;
	
	GameController *g = get_game_by_id(gid);
	if (!g)
		return false;
	
	if (g->getOwner() != client->id && !(client->state & Authed))
		return false;
	
	g->pause();
	
	return true;
}

bool client_cmd_request_gameresume(clientcon *client, Tokenizer &t)
{
	int gid;
	t >> gid;
	
	GameController *g = get_game_by_id(gid);
	if (!g)
		return false;
	
	if (g->getOwner() != client->id && !(client->state & Authed))
		return false;
	
	g->resume();
	
	return true;
}

int client_cmd_request(clientcon *client, Tokenizer &t)
{
	if (!t.count())
	{
		send_err(client, ErrParameters);
		return 1;
	}
	
	bool cmderr = false;
	
	string request;
	t >> request;
	
	if (request == "clientinfo")
		cmderr = !client_cmd_request_clientinfo(client, t);
	else if (request == "gameinfo")
		cmderr = !client_cmd_request_gameinfo(client, t);
	else if (request == "gamelist")
		cmderr = !client_cmd_request_gamelist(client, t);
	else if (request == "playerlist")
		cmderr = !client_cmd_request_playerlist(client, t);
	else if (request == "serverinfo")
		cmderr = !client_cmd_request_serverinfo(client, t);
	else if (request == "start")
		cmderr = !client_cmd_request_gamestart(client, t);
	else if (request == "restart")
		cmderr = !client_cmd_request_gamerestart(client, t);
	else if (request == "pause")
		cmderr = !client_cmd_request_gamepause(client, t);
	else if (request == "resume")
		cmderr = !client_cmd_request_gameresume(client, t);
	else
		cmderr = true;
	
	if (!cmderr)
    	;//send_ok(client);
	else
		send_err(client);
	
	return 0;
}

bool send_playerlist_all(int gid)
{
    // send playerlist to all registered players
	vector<int> player_list;
	GameController *g = get_game_by_id(gid);
	g->getPlayerList(player_list);
    for (vector<int>::iterator e = player_list.begin(); e != player_list.end(); e++) {
		clientcon* player = get_client_by_id(*e);
        log_msg("game ", "sending playerlist to player %d", *e);
        send_playerlist(gid, player);
    }

    return true;
}

int client_cmd_rebuy(clientcon *client, Tokenizer &t)
{
	if (!t.count())
	{
		send_err(client, ErrParameters);
		return 1;
	}
	
	int gid;
	t >> gid;
    int rebuy_stake;
    t >> rebuy_stake;
    int player_id;
    t >> player_id;
	
	GameController *g = get_game_by_id(gid);
	if (!g)
	{
		send_err(client, 0 /*FIXME*/, "game does not exist");
		return 1;
	}

	log_msg("game ", "rebuying %d for user %d in game %d", rebuy_stake, player_id, gid);
	
	if (!g->isPlayer(player_id))
	{
		send_err(client, 0 /*FIXME*/, "you are not registered");
		return 1;
	}
	
	if (!g->rebuy(player_id, rebuy_stake))
	{
		send_err(client, 0 /*FIXME*/, "unable to rebuy");
		return 1;
	}
	
	
	log_msg("client ", "player %d rebought stake %d", player_id, rebuy_stake);
	
	return 0;
}

int client_cmd_respite(clientcon *client, Tokenizer &t)
{
	if (!t.count())
	{
		send_err(client, ErrParameters);
		return 1;
	}
	
	int gid;
	t >> gid;
    int respite;
    t >> respite;
	
	GameController *g = get_game_by_id(gid);
	if (!g)
	{
		send_err(client, 0 /*FIXME*/, "game does not exist");
		return 1;
	}

	log_msg("game ", "adding timeout %d for user %d in game %d", respite, client->id, gid);
	
	if (!g->isPlayer(client->id))
	{
		send_err(client, 0 /*FIXME*/, "you are not registered");
		return 1;
	}
	
	if (!g->addTimeout(client->id, respite))
	{
		send_err(client, 0 /*FIXME*/, "unable to add timeout");
		return 1;
	}
	
	
	log_msg("client ", "player %d added timeout %d", client->id, respite);
	
	return 0;
}

int client_cmd_register(clientcon *client, Tokenizer &t)
{
	if (!t.count())
	{
		send_err(client, ErrParameters);
		return 1;
	}
	
	int gid;
	t >> gid;
    int player_stake;
    t >> player_stake;
	
	string passwd = "";
	if (t.count() >=3)
		t >> passwd;
	
	GameController *g = get_game_by_id(gid);
	if (!g)
	{
		send_err(client, 0 /*FIXME*/, "game does not exist");
		return 1;
	}

	log_msg("game ", "registering user %d to game %d", client->id, gid);
    vector<int> player_list;
    g->getPlayerList(player_list);
    for (vector<int>::iterator e1 = player_list.begin(); e1 != player_list.end(); e1++) {
        log_msg("game ", " found player %d for game %d", *e1, gid);
    }

	
	if (g->isStarted() && g->getGameType() != GameController::RingGame)
	{
		send_err(client, 0 /*FIXME*/, "cannot join game after it's started");
		return 1;
	}
	
	if (g->isPlayer(client->id) )
	{
        if (g->getGameType() == GameController::RingGame)
		{
            if(!g->resumePlayer(client->id)) 
			{
                send_err(client, 0 /*FIXME*/, "Could not resume player");
                return 1;
            } 

            send_ok_game(gid, client);
            // send gameinfo so user gbc can update user_game_history.joined_at = 0 for current user
            send_gameinfo(client, gid);
            // send playerlist to all registered players
            send_playerlist_all(gid);
            return 0;
        } 
		else 
		{
            send_err(client, 0 /*FIXME*/, "you are already registered");
            return 1;
        }
	}
	
	// check for max-games-register limit
	const unsigned int register_limit = config.getInt("max_register_per_player");
	unsigned int count = 0;
	for (games_type::const_iterator e = games.begin(); e != games.end(); e++)
	{
		GameController *g = e->second;
		
		if (g->isPlayer(client->id))
		{
			if (++count == register_limit)
			{
				send_err(client, 0 /*FIXME*/, "register limit per player is reached");
				return 1;
			}
		}
	}
	
	if (!g->addPlayer(client->id, client->uuid, player_stake))
	{
		send_err(client, 0 /*FIXME*/, "unable to register");
		return 1;
	}
	
	
	log_msg("client ", "%s (%d) joined game %d (%d/%d)",
		client->info.name, client->id, gid,
		g->getPlayerCount(), g->getPlayerMax());
	
	
    send_ok_game(gid, client);

    // send gameinfo so user gbc can update user_game_history.joined_at = 0 for current user
    send_gameinfo(client, gid);

    // send playerlist to all registered players
    send_playerlist_all(gid);
	
	return 0;
}

int unregister_game(clientcon *client, int gid)
{
	GameController *g = get_game_by_id(gid);
	if (!g)
	{
		send_err(client, 0 /*FIXME*/, "game does not exist");
		return 1;
	}

	if (!g->isPlayer(client->id))
	{
		send_err(client, 0 /*FIXME*/, "you are not registered");
		return 1;
	}
	
	if ((!g->isCreated()) && g->getGameType() != GameController::RingGame)
	{
		send_err(client, 0 , "leaving game is not allowed for non-Sit&Go games when the game is not in Waiting state");
		return 1;
	}
	
	
	if (!g->removePlayer(client->id))
	{
		send_err(client, 0 /*FIXME*/, "unable to unregister");
		return 1;
	}
	
	
	log_msg("game", "%s (%d) parted game %d (%d/%d)",
		client->info.name, client->id, gid,
		g->getPlayerCount(), g->getPlayerMax());
	
	
    // send playerlist to all registered players, the player who just unregistered the game will not receive playerlist
    send_playerlist_all(gid);


    // need to send gid ?
	send_ok(client);
	
	return 0;
}

int client_cmd_unregister(clientcon *client, Tokenizer &t)
{
	if (!t.count())
	{
		send_err(client, ErrParameters);
		return 1;
	}
	
	int gid;
	t >> gid;
	
    if (gid != -1) {
        //unregister a specific game
        return unregister_game(client, gid);
    } else {
        //unregister all games
        for (games_type::const_iterator e = games.begin(); e != games.end(); e++) {
            int id = e->first;
            unregister_game(client, id);
        }
    }

    return 0;
}

int client_cmd_subscribe(clientcon *client, Tokenizer &t)
{
	if (!t.count())
	{
		send_err(client, ErrParameters);
		return 1;
	}
	
	int gid;
	t >> gid;
	
	string passwd = "";
	if (t.count() >=2)
		t >> passwd;
	
	GameController *g = get_game_by_id(gid);
	if (!g)
	{
		send_err(client, 0 /*FIXME*/, "game does not exist");
		return 1;
	}
	
	if (g->isSpectator(client->id))
	{
		send_err(client, 0 /*FIXME*/, "you are already subscribed");
		return 1;
	}
	
	// check for max-games-subscribe limit
	const unsigned int subscribe_limit = config.getInt("max_subscribe_per_player");
	unsigned int count = 0;
	for (games_type::const_iterator e = games.begin(); e != games.end(); e++)
	{
		GameController *g = e->second;
		
		if (g->isSpectator(client->id))
		{
			if (++count == subscribe_limit)
			{
				send_err(client, 0 /*FIXME*/, "subscribe limit per player is reached");
				return 1;
			}
		}
	}

	// password is control by gbc
    /*
	if (!g->checkPassword(passwd))
	{
		send_err(client, 0 , "unable to subscribe, wrong password");
		return 1;
	}
    */
	
	if (!g->addSpectator(client->id))
	{
		send_err(client, 0 /*FIXME*/, "unable to subscribe");
		return 1;
	}
	
	
	log_msg("game", "%s (%d) subscribed game %d",
		client->info.name, client->id, gid);
	
	
	send_ok(client);
	
	return 0;
}

int client_cmd_unsubscribe(clientcon *client, Tokenizer &t)
{
	if (!t.count())
	{
		send_err(client, ErrParameters);
		return 1;
	}
	
	int gid;
	t >> gid;
	
	GameController *g = get_game_by_id(gid);
	if (!g)
	{
		send_err(client, 0 /*FIXME*/, "game does not exist");
		return 1;
	}
	
	if (!g->isSpectator(client->id))
	{
		send_err(client, 0 /*FIXME*/, "you are not subscribed");
		return 1;
	}
	
	if (!g->removeSpectator(client->id))
	{
		send_err(client, 0 /*FIXME*/, "unable to unsubscribe");
		return 1;
	}
	
	
	log_msg("game", "%s (%d) unsubscribed game %d",
		client->info.name, client->id, gid);
	
	
	send_ok(client);
	
	return 0;
}

int client_cmd_action(clientcon *client, Tokenizer &t)
{
	if (t.count() < 2)
	{
		send_err(client, ErrParameters);
		return 1;
	}
	
	int gid;
	string action;
	chips_type arg;
	
	t >> gid >> action;
	arg = t.getNextInt();
	
	GameController *g = get_game_by_id(gid);
	if (!g)
	{
		send_err(client, 0 /* FIXME */, "game does not exist");
		return 1;
	}
	
	Player::PlayerAction a = Player::None;
	
	if (action == "check")
		a = Player::Check;
	else if (action == "fold")
		a = Player::Fold;
	else if (action == "call")
		a = Player::Call;
	else if (action == "bet")
		a = Player::Bet;
	else if (action == "raise")
		a = Player::Raise;
	else if (action == "allin")
		a = Player::Allin;
	else if (action == "show")
		a = Player::Show;
	else if (action == "muck")
		a = Player::Muck;
	else if (action == "sitout")
		a = Player::Sitout;
	else if (action == "back") {
		a = Player::Back;
        send_playerlist(gid, client);
    }
	else if (action == "reset")
		a = Player::ResetAction;
	else
	{
		send_err(client, ErrParameters);
		return 1;
	}
	
	
	g->setPlayerAction(client->id, a, arg);
	
	return 0;
}

int client_cmd_nextroundstraddle(clientcon *client, Tokenizer &t)
{
	if (!t.count())
	{
		send_err(client, ErrParameters);
		return 1;
	}

	int gid;
	t >> gid;

	GameController *g = get_game_by_id(gid);
	if (!g)
	{
		send_err(client, 0 /*FIXME*/, "game does not exist");
		return 1;
	}

	if (!g->isPlayer(client->id))
	{
		send_err(client, 0 /*FIXME*/, "you are not registered");
		return 1;
	}

	if (!g->nextRoundStraddle(client->id))
	{
		send_err(client, 0 /*FIXME*/, "unable to straddle");
		return 1;
	}

	log_msg("client ", "player %d next round straddle", client->id);

	return 0;
}

int client_cmd_buy_insurance(clientcon *client, Tokenizer &t)
{
	if (t.count() < 3)
	{
		send_err(client, ErrParameters);
		return 1;
	}

	int gid;
	t >> gid;

	int buy_amount;
	t >> buy_amount;

	string scard;
	vector<Card> cards;
	while (t.getNext(scard))
	{
		Card card(scard.c_str());
		cards.push_back(card);
	}
	GameController *g = get_game_by_id(gid);
	if (!g)
	{
		send_err(client, 0 /*FIXME*/, "game does not exist");
		return 1;
	}
	if (!g->isPlayer(client->id))
	{
		send_err(client, 0 /*FIXME*/, "you are not registered");
		return 1;
	}
	if (!g->clientBuyInsurance(client->id, buy_amount, cards))
	{
		send_err(client, 0 /*FIXME*/, "unable to buy insurance");
		return 1;
	}

	return 0;
}

int client_cmd_create(clientcon *client, Tokenizer &t)
{
	if (!config.getBool("perm_create_user") && !(client->state & Authed))
	{
		send_err(client, ErrNoPermission, "no permission");
		return 1;
	}
	
	// check for server games count limit
	if (games.size() >= (unsigned int) config.getInt("max_games"))
	{
		send_err(client, 0 /*FIXME*/, "server games count reached");
		return 1;
	}
	
	
	// check for max-games-create limit
	unsigned int create_limit = config.getInt("max_create_per_player");
	unsigned int count = 0;
	for (games_type::const_iterator e = games.begin(); e != games.end(); e++)
	{
		GameController *g = e->second;
		
		if (g->getOwner() == client->id)
		{
			if (++count == create_limit)
			{
				send_err(client, 0 /*FIXME*/, "create limit per player is reached");
				return 1;
			}
		}
	}
	
	bool cmderr = false;
	
	struct {
		string name;
		unsigned int max_players;
		int type;
        unsigned int game_id;
		chips_type stake;
		unsigned int timeout;
		chips_type blinds_start;
		unsigned int blinds_factor;
		unsigned int blinds_time;
		unsigned int ante;
		bool mandatory_straddle;
		string password;
		bool restart;
        int expire_in;
	    bool enable_insurance;
    } ginfo = {
		"user_game",
		9,
		GameController::SNG,
        0,
		1500,
		30,
		20,
		20,
		180,
		0,
		false,
		"",
		false,
        30 * 60,
        true
	};
	
	
	string infostr;
	Tokenizer it(":");
	
	while (t.getNext(infostr))
	{
		it.parse(infostr);
		
		string infotype, infoarg;
		it.getNext(infotype);
		
		bool havearg = it.getNext(infoarg);
		
		if (infotype == "type" && havearg)
		{
			ginfo.type = Tokenizer::string2int(infoarg);
			
			if (ginfo.type != GameController::SNG 
             && ginfo.type != GameController::FreezeOut
             && ginfo.type != GameController::RingGame)
				cmderr = true;
		}
		else if (infotype == "game_id" && havearg)
		{
			ginfo.game_id = Tokenizer::string2int(infoarg);
			
			if (ginfo.game_id < 0 || ginfo.game_id > UINT_MAX)
				cmderr = true;
		}
		else if (infotype == "players" && havearg)
		{
			ginfo.max_players = Tokenizer::string2int(infoarg);
			
			if (ginfo.max_players < 2 
                || (ginfo.type == GameController::SNG && ginfo.max_players > 9)
                || (ginfo.type == GameController::FreezeOut && ginfo.max_players > 1000 * 10)
               )
				cmderr = true;
		}
		else if (infotype == "stake" && havearg)
		{
			ginfo.stake = Tokenizer::string2int(infoarg);
			
			if (ginfo.stake < 10 || ginfo.stake > 1000000*100)
				cmderr = true;
		}
		else if (infotype == "timeout" && havearg)
		{
			ginfo.timeout = Tokenizer::string2int(infoarg);
			
			if (ginfo.timeout < 5 || ginfo.timeout > 10*60)
				cmderr = true;
		}
		else if (infotype == "name" && havearg)
		{
			if (infoarg.length() > 50)
				infoarg = string(infoarg, 0, 50);
			
			ginfo.name = infoarg;
		}
		else if (infotype == "blinds_start" && havearg)
		{
			ginfo.blinds_start = Tokenizer::string2int(infoarg);
			
			if (ginfo.blinds_start < 1 || ginfo.blinds_start > 200*100)
				cmderr = true;
		}
		else if (infotype == "blinds_factor" && havearg)
		{
			ginfo.blinds_factor = Tokenizer::string2int(infoarg);
			
			if (ginfo.blinds_factor < 12 || ginfo.blinds_factor > 40)
				cmderr = true;
		}
		else if (infotype == "blinds_time" && havearg)
		{
			ginfo.blinds_time = Tokenizer::string2int(infoarg);
			
			if (ginfo.blinds_time < 30 || ginfo.blinds_time > 30*60)
				cmderr = true;
		}
		else if (infotype == "ante" && havearg)
		{
			ginfo.ante = Tokenizer::string2int(infoarg);
            log_msg("Ante", "create game ante %d", ginfo.ante);
		}
		else if (infotype == "mandatory_straddle" && havearg)
		{
			ginfo.mandatory_straddle = Tokenizer::string2int(infoarg) ? 1 : 0;
		    log_msg("Straddle", "create game mandatory_straddle %d", ginfo.mandatory_straddle);
        }
		else if (infotype == "password" && havearg)
		{
			if (infoarg.length() > 16)
				infoarg = string(infoarg, 0, 16);
			
			ginfo.password = infoarg;
		}
		else if (infotype == "restart" && havearg)
		{
			if (client->state & Authed)
				ginfo.restart = Tokenizer::string2int(infoarg) ? 1 : 0;
			else
				cmderr = true;
		}
		else if (infotype == "expire_in" && havearg)
		{
            ginfo.expire_in = Tokenizer::string2int(infoarg);
            
            if (ginfo.expire_in < 0)
                cmderr = true;
		}
        else if (infotype == "enable_insurance" && havearg)
        {
            log_msg("game", "param enable_insurance");
            ginfo.enable_insurance = Tokenizer::string2int(infoarg) ? 1 : 0;
        }
	}
	
	if (!cmderr)
	{
		GameController *g = NULL;
        if (ginfo.type == GameController::RingGame) {
            g = new SitAndGoGameController();
            ((SitAndGoGameController*)g)->setExpireIn(ginfo.expire_in);
        } else if (ginfo.type == GameController::SNG) {
            g = new SNGGameController();
        }

        const int gid = ginfo.game_id;
		g->setGameId(gid);
		g->setPlayerMax(ginfo.max_players);
		g->setPlayerTimeout(ginfo.timeout);
		g->setPlayerStakes(ginfo.stake);
		g->setOwner(client->id);
		g->setName(ginfo.name);
		g->setBlindsStart(ginfo.blinds_start);
		g->setBlindsFactor(ginfo.blinds_factor);
		g->setBlindsTime(ginfo.blinds_time);
		g->setAnte(ginfo.ante);
		g->setMandatoryStraddle(ginfo.mandatory_straddle);
		g->setPassword(ginfo.password);
		g->setRestart(ginfo.restart);
		g->setEnableInsurance(ginfo.enable_insurance);
        games[gid] = g;

		log_msg("game", "%s (%d) created game %d, enable_insurance = %d",
			client->info.name, client->id, gid, ginfo.enable_insurance);
		
		// update stats
		stats.games_created++;
        send_gameinfo(client, gid);

        // send playerlist to creater
        send_playerlist(gid, client);
	}
	else
		send_err(client);
	
	return 0;
}

int client_cmd_auth(clientcon *client, Tokenizer &t)
{
	bool cmderr = true;
	
	if (t.count() >= 2 && config.get("auth_password").length())
	{
		const int type = t.getNextInt();
		const string passwd = t.getNext();
		
		// -1 is server-auth
		if (type == -1)
		{
			if (passwd == config.get("auth_password"))
			{
				client->state |= Authed;
				
				cmderr = false;
			}
		}
	}
	
	if (!cmderr)
	{
		log_msg("auth", "%s (%d) has been authed",
			client->info.name, client->id);
	
		send_ok(client);
	}
	else
		send_err(client, 0, "auth failed");
	
	return 0;
}

int client_cmd_config(clientcon *client, Tokenizer &t)
{
	bool cmderr = false;
	
	if (client->state & Authed)
	{
		const string action = t.getNext();
		const string varname = t.getNext();
		
		if (action == "get")
		{
			if (config.exists(varname))
				snprintf(msg, sizeof(msg), "Config: %s=%s",
					varname.c_str(),
					config.get(varname).c_str());
			else
				snprintf(msg, sizeof(msg), "Config: %s not set",
					varname.c_str());
			client_chat(-1, client->id, msg);
		}
		else if (action == "set")
		{
			const string varvalue = t.getNext();
			
			config.set(varname, varvalue);
			
			log_msg("config", "%s (%d) set var '%s' to '%s'",
				client->info.name, client->id,
				varname.c_str(), varvalue.c_str());
		}
		else if (action == "save")
		{
			char cfgfile[1024];
			snprintf(cfgfile, sizeof(cfgfile), "%s/server.cfg", sys_config_path());
			
			// update config-version and save config
			config.set("version", VERSION);
			config.save(cfgfile);
			
			client_chat(-1, client->id, "Config saved");
		}
		else
			cmderr = true;
	}
	else
		cmderr = true;
	
	if (!cmderr)
		send_ok(client);
	else
		send_err(client, 0, "config request failed");
	
	return 0;
}

int client_execute(clientcon *client, const char *cmd)
{
	Tokenizer t(" ");
	t.parse(cmd);  // parse the command line
	
	// ignore blank command
	if (!t.count())
		return 0;
	
	//dbg_msg("clientsock", "(%d) executing '%s'", client->sock, cmd);
	
	// FIXME: could be done better...
	// extract message-id if present
	const char firstchar = t[0][0];
	if (firstchar >= '0' && firstchar <= '9')
		client->last_msgid = t.getNextInt();
	else
		client->last_msgid = -1;
	
	
	// get command argument
	const string command = t.getNext();
	
	//log_msg("client execute", "cmd = %s", command.c_str());
	if (!(client->state & Introduced))  // state: not introduced
	{
		if (command == "PCLIENT")
			return client_cmd_pclient(client, t);
		else
		{
			// seems not to be a pclient
			send_err(client, ErrProtocol, "protocol error");
			return -1;
		}
	}
	else if (command == "INFO")
		return client_cmd_info(client, t);
	else if (command == "CHAT")
		return client_cmd_chat(client, t);
	else if (command == "REQUEST")
		return client_cmd_request(client, t);
	else if (command == "REBUY")
		return client_cmd_rebuy(client, t);
	else if (command == "RESPITE")
		return client_cmd_respite(client, t);
	else if (command == "REGISTER")
		return client_cmd_register(client, t);
	else if (command == "UNREGISTER")
		return client_cmd_unregister(client, t);
	else if (command == "SUBSCRIBE")
		return client_cmd_subscribe(client, t);
	else if (command == "UNSUBSCRIBE")
		return client_cmd_unsubscribe(client, t);
	else if (command == "ACTION")
		return client_cmd_action(client, t);
	else if (command == "CREATE")
		return client_cmd_create(client, t);
	else if (command == "AUTH")
		return client_cmd_auth(client, t);
	else if (command == "CONFIG")
		return client_cmd_config(client, t);
	else if (command == "STRADDLE")
		return client_cmd_nextroundstraddle(client, t);
	else if (command == "BUYINSURANCE")
		return client_cmd_buy_insurance(client, t);
	else if (command == "QUIT")
	{
		send_ok(client);
		return -1;
	}
	else
		send_err(client, ErrNotImplemented, "not implemented");
	
	return 0;
}

// returns zero if no cmd was found or no bytes remaining after exec
int client_parsebuffer(clientcon *client)
{
	//log_msg("clientsock", "(%d) parse (bufferlen=%d)", client->sock, client->buflen);
	
	int found_nl = -1;
	for (int i=0; i < client->buflen; i++)
	{
		if (client->msgbuf[i] == '\r')
			client->msgbuf[i] = ' ';  // space won't hurt
		else if (client->msgbuf[i] == '\n')
		{
			found_nl = i;
			break;
		}
	}
	
	int retval = 0;
	
	// is there a command in queue?
	if (found_nl != -1)
	{
		// extract command
		char cmd[sizeof(client->msgbuf)];
		memcpy(cmd, client->msgbuf, found_nl);
		cmd[found_nl] = '\0';
		
		//log_msg("clientsock", "(%d) command: '%s' (len=%d)", client->sock, cmd, found_nl);
		if (client_execute(client, cmd) != -1)  // client quitted ?
		{
			// move the rest to front
			memmove(client->msgbuf, client->msgbuf + found_nl + 1, client->buflen - (found_nl + 1));
			client->buflen -= found_nl + 1;
			//log_msg("clientsock", "(%d) new buffer after cmd (bufferlen=%d)", client->sock, client->buflen);
			
			retval = client->buflen;
		}
		else
		{
			client_remove(client->sock);
			retval = 0;
		}
	}
	else
		retval = 0;
	
	return retval;
}

int client_handle(socktype sock)
{
	char buf[1024];
	int bytes;
	
	// return early on client close/error
	if ((bytes = socket_read(sock, buf, sizeof(buf))) <= 0)
		return bytes;
	
	
	//log_msg("clientsock", "(%d) DATA len=%d", sock, bytes);
	
	clientcon *client = get_client_by_sock(sock);
	if (!client)
	{
		log_msg("clientsock", "(%d) error: no client associated", sock);
		return -1;
	}
	
	if (client->buflen + bytes > (int)sizeof(client->msgbuf))
	{
		log_msg("clientsock", "(%d) error: buffer size exceeded", sock);
		client->buflen = 0;
	}
	else
	{
		memcpy(client->msgbuf + client->buflen, buf, bytes);
		client->buflen += bytes;
		
		// parse and execute all commands in queue
		while (client_parsebuffer(client));
	}
	
	return bytes;
}

void remove_expired_conar_entries()
{
	time_t curtime = time(NULL);
	unsigned int expire = config.getInt("conarchive_expire");
	
#if 0
	dbg_msg("clientar", "scanning for expired entries");
#endif
	
	for (clientconar_type::iterator e = con_archive.begin(); e != con_archive.end();)
	{
		clientcon_archive *conar = &(e->second);
		
		if (conar->logout_time && (unsigned int)difftime(curtime, conar->logout_time) > expire)
		{
			dbg_msg("clientar", "removing expired entry %s", e->first.c_str());
			con_archive.erase(e++);
		}
		else
			++e;
	}
}


int gameinit()
{
	// initialize server stats struct
	memset(&stats, 0, sizeof(server_stats));
	stats.server_started = time(NULL);
	
	
#ifndef NOSQLITE
	ranking_setup();
#endif /* NOSQLITE */
	
	
#ifdef DEBUG
	// initially add games for debugging purpose
	if (!games.size())
	{
		for (int i=0; i < config.getInt("dbg_testgame_games"); i++)
		{
			GameController *g = new GameController();
			const int gid = i;
			g->setGameId(gid);
			g->setName("test game");
			g->setRestart(true);
			g->setOwner(-1);
			g->setPlayerMax(config.getInt("dbg_testgame_players"));
			g->setPlayerTimeout(config.getInt("dbg_testgame_timeout"));
			g->setPlayerStakes(config.getInt("dbg_testgame_stakes"));
			
			if (config.getBool("dbg_stresstest") && i > 10)
			{
                for (int j=0; j < config.getInt("dbg_testgame_players"); j++)
                     g->addPlayer(j*1000 + i, "DEBUG");
            }
            games[gid] = g;
        }
    }
#endif
	
	return 0;
}

int gameloop()
{
	// handle all games
	for (games_type::iterator e = games.begin(); e != games.end();)
	{
		GameController *g = e->second;
		
		// game has been deleted
		int rc = g->tick();
		if (rc < 0)
		{
			// replicate game if "restart" is set
			if (g->getRestart())
			{
				const int gid = g->getGameId();
				GameController *newgame = new GameController(*g);
				
				// set new ID
				newgame->setGameId(gid);
				
				games[gid] = newgame;
				
				log_msg("game", "restarted game: %d ", g->getGameId());
			}
			else {
				log_msg("game", "deleting game %d", g->getGameId());
                games.erase(e++);
            }
			
            delete g;
		}
		else if (rc == 1 && !g->isFinished())  // game has ended (but not deleted)
        {
            g->setFinished();

            // send game info to all players so user_game_history.ended_at can be updated
            vector<int> player_list;
            g->getPlayerList(player_list);
            for (vector<int>::iterator e1 = player_list.begin(); e1 != player_list.end(); e1++) {
                clientcon* client = get_client_by_id(*e1);
                send_gameinfo(client, g->getGameId());
            }
#ifndef NOSQLITE
            ranking_update(g);
#endif /* !NOSQLITE */

            ++e;
        }
		else
			++e;
	}
	
	
	// delete all expired archived connection-data (con_archive)
	if ((unsigned int)difftime(time(NULL), last_conarchive_cleanup) > 5 * 60)
	{
		remove_expired_conar_entries();
		
		last_conarchive_cleanup = time(NULL);
	}
	
	return 0;
}
