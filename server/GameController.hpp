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


#ifndef _GAMECONTROLLER_H
#define _GAMECONTROLLER_H

#include <vector>
#include <string>
#include <map>
#include <set>
#include <string>
#include <ctime>

#include "Card.hpp"
#include "Deck.hpp"
#include "HoleCards.hpp"
#include "CommunityCards.hpp"
#include "Table.hpp"
#include "Player.hpp"
#include "GameLogic.hpp"


class GameController
{
friend class TestCaseGameController;

public:
	typedef std::map<int,Table*>	tables_type;
	typedef std::map<int,Player*>	players_type;
	typedef std::set<int>		spectators_type;
	
	typedef std::vector<Player*>	finish_list_type;
	
	typedef enum {
		RingGame = 0x01,   // Cash game
		FreezeOut,  // Tournament
		SNG         // Sit'n'Go
	} GameType;

	typedef enum {
        Created = 0x00,
		Started = 0x01,  
		Paused  = 0x02,  
		Ended   = 0x03,
        Expired = 0x04,
        Finished= 0x05
	} GameStatus;
	
	
	typedef enum {
        BlindNone,
		BlindByTime,
		//BlindByRound,
		BlindByKnockout
	} BlindRule;
	
	typedef enum {
		NoLimit,
		PotLimit,
		SplitLimit,
		FixedLimit
	} LimitRule;

	typedef struct {
		int level;
		chips_type big_blind;
		chips_type ante;
	} BlindLevel;
	
	GameController();
	GameController(const GameController& g);
	virtual ~GameController();
	
	virtual void reset();
	
	bool setGameId(int gid) { game_id = gid; return true; };
	int getGameId() const { return game_id; };
	
	GameType getGameType() const { return type; };
    void setGameType(GameType game_type) { type = game_type; };
	
	GameStatus getGameStatus() const { return status; };
    void setGameStatus(GameStatus game_status) { status = game_status; };

	void setPlayerTimeout(unsigned int respite) { timeout = respite; };
	unsigned int getPlayerTimeout() const { return timeout; };

	void setAnte(unsigned int val) { ante = val; };
	unsigned int getAnte() const { return ante;  };

	void setMandatoryStraddle(bool val) { mandatory_straddle = val; };
	bool getMandatoryStraddle() const { return mandatory_straddle; };

	void setBlindsStart(chips_type blinds_start) { blind.start = blinds_start; };
	chips_type getBlindsStart() const { return blind.start; };
	void setBlindsFactor(unsigned int blinds_factor) { blind.blinds_factor = blinds_factor; };
	unsigned int getBlindsFactor() const { return blind.blinds_factor; };
	void setBlindsTime(unsigned int blinds_time) { blind.blinds_time = blinds_time; };
	unsigned int getBlindsTime() const { return blind.blinds_time; };
	void setBlindRule(BlindRule rule){ blind.blindrule = rule; };
	BlindRule getBlindRule() const { return blind.blindrule; };
	
	bool setPlayerStakes(chips_type stake);
	chips_type getPlayerStakes() const { return player_stakes; };
	
	std::string getName() const { return name; };
	bool setName(const std::string &str) { name = str; return true; }; // FIXME: validate
	
	bool checkPassword(const std::string &passwd) const { return (!password.length() || password == passwd); };
	bool hasPassword() const { return password.length(); };
	bool setPassword(const std::string &str) { password = str; return true; };
	std::string getPassword() const { return password; };
	
	bool setPlayerMax(unsigned int max);
	unsigned int getPlayerMax() const { return max_players; };
	unsigned int getPlayerCount() const { return players.size(); };
	
	virtual bool getPlayerList(std::vector<int> &client_list, bool including_wanna_leave = false) const {return true;};
	virtual bool getPlayerList(std::vector<std::string> &client_list) const {return true;};
	bool getListenerList(std::vector<int> &client_list) const;
	void getFinishList(std::vector<Player*> &player_list) const;
	
	void setRestart(bool bRestart) { restart = bRestart; };
	bool getRestart() const { return restart; };

	
	bool isStarted() const { return status == Started; };
	bool isCreated() const { return status == Created; };
	bool isPaused() const { return status == Paused; };
	bool isEnded() const { return status == Ended; };
	
	bool isFinished() const { return status == Finished; };
	void setFinished() { status = Finished; };
	
    virtual void handleRebuy(Table *t) {return;};
    bool rebuy(int cid, chips_type rebuy_stake);
	bool addPlayer(int cid, const std::string &uuid);
	virtual bool clientBuyInsurance(int cid, int buy_amount, std::vector<Card> & cards) {return false; };
	virtual bool nextRoundStraddle(int cid) { return false; };
    virtual bool addPlayer(int cid, const std::string &uuid, chips_type player_stake){return true;};
	virtual bool removePlayer(int cid) {return true;};
	virtual bool resumePlayer(int cid) {return true;};
	virtual bool handleBuyInsurance(Table *t, unsigned int round);
	bool isPlayer(int cid) const;
    bool addTimeout(int cid, unsigned int timeout_to_add);
	
	bool addSpectator(int cid);
	bool removeSpectator(int cid);
	bool isSpectator(int cid) const;
	
	void setOwner(int cid) { owner = cid; };
	int getOwner() const { return owner; };
	
	void chat(int tid, const char* msg);
	void chat(int cid, int tid, const char* msg);
	
	bool setPlayerAction(int cid, Player::PlayerAction action, chips_type amount);
	
	virtual void start() {return ;};
    void pause();
    void resume();
	
	virtual int tick() {return 0;};
	
	
	Player* findPlayer(int cid);
	void selectNewOwner();
	
	void snap(int tid, int sid, const char* msg="");
	void snap(int cid, int tid, int sid, const char* msg="");
	
	bool createWinlist(Table *t, std::vector< std::vector<HandStrength> > &winlist);
	chips_type determineMinimumBet(Table *t) const;
	
	virtual int handleTable(Table *t) {return 0;};
	virtual void stateSuspend(Table *t);
	virtual void stateResume(Table * t);
	virtual void stateNewRound(Table *t) ;
	virtual void stateBlinds(Table *t) ;
	virtual void stateBetting(Table *t) {return;};
	void stateBettingEnd(Table *t);   // pseudo-state
	void stateAskShow(Table *t);
	void stateAllFolded(Table *t);
	void stateShowdown(Table *t);
	virtual void stateEndRound(Table *t) {return ;};
	
	// pseudo-state for delays
	void stateDelay(Table *t);
	
	void dealHole(Table *t);
	void dealFlop(Table *t);
	void dealTurn(Table *t);
	void dealRiver(Table *t);
	
	void sendTableSnapshot(Table *t);
	void sendPlayerShowSnapshot(Table *t, Player *p);
	
    virtual void placePlayers() {return;};
    void placeTable(int offset, int total_players);
    std::vector<int> calcTables(int players_to_arrange);

    virtual void addBlindLevels() ;
      
	int game_id;
	
    time_t started_time;
    bool paused;
	unsigned int max_players;
	
	GameType type;
    GameStatus status;
	LimitRule limit;
	chips_type player_stakes;
	unsigned int timeout;
	
	players_type		players;
	spectators_type		spectators;
	tables_type		tables;
	
	struct {
		chips_type start;
		chips_type amount;
		BlindRule blindrule;
		unsigned int blinds_time;  // seconds
		time_t last_blinds_time;
		unsigned int blinds_factor;
        size_t level;
	} blind;

	unsigned int ante;
	bool mandatory_straddle;
	

	unsigned int hand_no;
	
	int owner;   // owner of a game
	bool restart;   // should be restarted when ended?
	
	time_t ended_time;

	
	finish_list_type finish_list;
	
	std::string name;
	std::string password;

    int tid;

    std::vector<BlindLevel> blind_levels;
	
#ifdef DEBUG
	std::vector<Card> debug_cards;
#endif
};

#endif /* _GAMECONTROLLER_H */
