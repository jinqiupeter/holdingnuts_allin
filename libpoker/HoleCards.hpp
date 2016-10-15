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


#ifndef _HOLECARDS_H
#define _HOLECARDS_H

#include <vector>
#include <string>

#include "Card.hpp"

class HoleCards
{
public:
	HoleCards();
	
	bool setCards(Card c1, Card c2);
    bool setShowCard(int which, bool show);
	void clear() { cards.clear(); showcards.clear();};
	
	void copyCards(std::vector<Card> *v) const { v->insert(v->end(), cards.begin(), cards.end()); };
    std::string showCards();
	Card * getC1() { if (cards.size() > 0) return &cards[0]; else return NULL; };
	Card * getC2() { if (cards.size() > 1) return &cards[1]; else return NULL; };
	
	void debug();
private:
	std::vector<Card> cards;
	std::vector<bool> showcards;
};

#endif /* _HOLECARDS_H */
