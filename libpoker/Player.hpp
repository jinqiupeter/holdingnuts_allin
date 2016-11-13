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


#ifndef _PLAYER_H
#define _PLAYER_H

#include <string>
#include <map>
#include <set>

#include "HoleCards.hpp"

typedef unsigned int chips_type;



class Player
{
friend class GameController;
friend class SitAndGoGameController;
friend class SNGGameController;
friend class TestCaseGameController;
friend class Table;

public:
	typedef enum {
		None,
		ResetAction,
		
		Check,
		Fold,
		Call,
		Bet,
		Raise,
		Allin,
		
		Show,
		Muck,
		
		Sitout,
		Back
	} PlayerAction;
	
	typedef struct {
		bool valid;
		PlayerAction action;
		chips_type amount;
	} SchedAction;

	typedef struct {
		// �Ƿ��ѹ���
		bool bought;
		// ����������⸶
		chips_type max_payment;
		// ������
		chips_type buy_amount;
		// outs
		std::vector<Card> outs;
	    std::vector<Card> outs_divided;
        // ����outs
        std::map<int, std::vector<Card> > every_single_outs;
		// �����outs
		std::vector<Card> buy_cards;
		// �����Ǯ
		chips_type res_amount;
        std::vector<chips_type> buy_pots;
        std::vector<chips_type> pots_investment;
    }InsuranceInfo;
	
	Player();
	
	chips_type getStake() const { return stake; };
    void setStake(chips_type aStake) {stake = aStake; };

	chips_type getRebuyStake() const { return rebuy_stake; };
    void setRebuyStake(chips_type aStake) {rebuy_stake = aStake; };

	int getTimedoutCount() const { return timedout_count; };
    void setTimedoutCount(int count) {timedout_count = count; };

	unsigned int getTimeout() const { return timeout; };
    void setTimeout(int time_out) {timeout = time_out; };

	int getClientId() const { return client_id; };
	
	const std::string& getPlayerUUID() const { return uuid; };
	
	void resetLastAction() { last_action = Player::None; }
	
    void setTableNo(int number) { table_no = number; };	
    int getTableNo() { return table_no; };

    void setSeatNo(int number) { seat_no = number; };	
    int getSeatNo() { return seat_no; };
    void clearInsuranceInfo();
private:
	int client_id;
	
	// NOTE: redundant information here, because client may disconnect
	std::string uuid;	/* copy of uuid needed */
	
	chips_type stake;		// currrent stake
	chips_type stake_before;	// stake before new hand
	chips_type rebuy_stake;	// rebuy stake is added to stake in Table.NewRound
	
	HoleCards holecards;
	
	SchedAction next_action;
	PlayerAction last_action;
	
	bool sitout;     // is player sitting out?
    bool wanna_leave;

    int table_no;
    int seat_no;
    int timedout_count; // if player timed out > 3 times, mark player as wanna_leave
    unsigned int timeout; //each user has different timeout, to support buy timeout
	InsuranceInfo insuraceInfo[2];
};

#endif /* _PLAYER_H */

