#include <vector>
#include "SearchEnvironment.h"
#include "MultiAgentEnvironment.h"
#include "Minimax.h" // for the function CRHash
#include <ext/hash_set>
#include <map>

#ifndef TIDASTAR_H
#define TIDASTAR_H

/*!
	two players IDA* implementation
	min player (cop) plays first by default (parameter minFirst in tida)
	pos is a tuple of locations of robber (pos[0]) and cop (pos[1])

	This version is more optimized than the version in TIDAStar.h
	and was used to generate our statistics for
	"Optimal Solutions for Moving Target Search" (2008)
*/
template<class state, class action, class environment>
class TIDAStar {

	public:

	typedef typename MultiAgentEnvironment<state,action>::MAState CRState;


	// used for heuristic updates
	struct CRStateHash {
		size_t operator()( const CRState &s ) const {
			return CRHash<state>( s );
		}
	};
	struct CRStateEqual {
		bool operator()( const CRState &c1, const CRState &c2 ) const {
			return ( c1 == c2 );
		}
	};
	typedef __gnu_cxx::hash_map<CRState, double, CRStateHash, CRStateEqual> BoundCache;


	// constructor
	TIDAStar( environment *_env, bool _canPause ):
		env(_env), canPause(_canPause) {};
	~TIDAStar();

	void clear_bounds_cache();

	double tida( CRState &pos, bool minFirst = true );

	unsigned int nodesExpanded, nodesTouched;
	std::vector<unsigned int> iteration_nodesExpanded, iteration_nodesTouched;

	protected:

	double tida_update( CRState &pos, double bound, bool minFirst );

	double MinHCost( CRState &pos, bool minsTurn = true );
	double MinGCost( CRState &pos1, CRState &pos2 );
	bool GoalTest( CRState &pos );
	double TerminalCost( CRState &pos );

	environment *env;
	bool canPause;

	// lower bound cache
	BoundCache min_lcache, max_lcache;
	// upper bound cache
	BoundCache min_ucache, max_ucache;
};




/*------------------------------------------------------------------------------
| Implementation
------------------------------------------------------------------------------*/
template<class state, class action, class environment>
TIDAStar<state,action,environment>::~TIDAStar() {
	clear_bounds_cache();
}


// public functions

template<class state, class action, class environment>
void TIDAStar<state,action,environment>::clear_bounds_cache() {
	min_lcache.clear();
	max_lcache.clear();
	min_ucache.clear();
	max_lcache.clear();
	return;
}


template<class state, class action, class environment>
double TIDAStar<state,action,environment>::tida( CRState &pos, bool minFirst ) {
	double b, c = MinHCost( pos, minFirst );

	unsigned int sumNodesTouched = 0, sumNodesExpanded = 0;

	do {
		nodesExpanded = 0; nodesTouched = 0;
		b = c;
		fprintf( stdout, "set bound to b = %f\n", b );
		c = tida_update( pos, b, minFirst );

		iteration_nodesExpanded.push_back( nodesExpanded );
		iteration_nodesTouched.push_back( nodesTouched );
		sumNodesExpanded += nodesExpanded;
		sumNodesTouched  += nodesTouched;

	} while( c > b ); // until c <= b

	nodesExpanded = sumNodesExpanded;
	nodesTouched  = sumNodesTouched;

	return c;
}


template<class state, class action, class environment>
double TIDAStar<state,action,environment>::tida_update( CRState &pos, double bound, bool minFirst )
{
	double result, temp, c;
	std::vector<state> neighbors;
	CRState neighbor;

	nodesTouched++;

	// verbose output
//	fprintf( stdout, "Considering position (%u,%u) (%u,%u) %d\n", pos[0].x, pos[0].y, pos[1].x, pos[1].y, minFirst );

	//if( GoalTest( pos ) ) return TerminalCost( pos );
	if( pos[0] == pos[1] ) return 0.;

	// upper bound cache lookup
	typename BoundCache::iterator hcit;
	BoundCache *current_bcache = minFirst?&min_ucache:&max_ucache;
	hcit = current_bcache->find( pos );
	if( hcit != current_bcache->end() ) {
		if( hcit->second <= bound ) return hcit->second;
	}

	// lower bound cache lookup
	current_bcache = minFirst?&min_lcache:&max_lcache;
	hcit = current_bcache->find( pos );
	if( hcit != current_bcache->end() ) {
		if( bound < hcit->second ) return hcit->second;
	} else {
		// heuristic pruning
		temp = MinHCost( pos, minFirst );
		if( bound < temp ) return temp;
	}

	nodesExpanded++;

	// in case we are the cop/min player
	if( minFirst ) {

		env->GetSuccessors( pos[1], neighbors );
		if( canPause ) neighbors.push_back( pos[1] );

		result = DBL_MAX;

		neighbor = pos;
		for( typename std::vector<state>::iterator iti = neighbors.begin(); iti != neighbors.end(); iti++ ) {
			neighbor[1] = *iti;
			c = MinGCost( pos, neighbor );
			temp = c + tida_update( neighbor, bound - c, !minFirst );

			result = min( temp, result );

			// alpha prune
			if( result <= bound ) break;
		}

	// in case we are the robber/max player
	} else {

		env->GetSuccessors( pos[0], neighbors );
		if( canPause ) neighbors.push_back( pos[0] );

		result = DBL_MIN;

		neighbor = pos;
		for( typename std::vector<state>::iterator iti = neighbors.begin(); iti != neighbors.end(); iti++ ) {
			neighbor[0] = *iti;
			c = MinGCost( pos, neighbor );
			temp = c + tida_update( neighbor, bound - c, !minFirst );

			result = max( temp, result );

			// beta pruning
			if( result > bound ) break;
		}

	}

	// update heuristic
	if( bound < result ) {
		current_bcache = minFirst?&min_lcache:&max_lcache;
		(*current_bcache)[pos] = result;
	} else {
		current_bcache = minFirst?&min_ucache:&max_ucache;
		(*current_bcache)[pos] = result;
	}

	return result;
}



// protected functions


template<class state, class action, class environment>
double TIDAStar<state,action,environment>::MinHCost( CRState &pos, bool minsTurn ) {
	if( canPause )
		return ( 2. * env->HCost( pos[1], pos[0] ) - (minsTurn?MinGCost(pos,pos):0.) );
	else
		// distance from cop to the robber
		return env->HCost( pos[1], pos[0] );
}

// specification for state=xyLoc
template<>
double TIDAStar<xyLoc,tDirection,MapEnvironment>::MinHCost( CRState &pos, bool minsTurn ) {

	double dist;
	dist = max( abs(pos[1].x - pos[0].x), abs(pos[1].y - pos[0].y) );

	if( canPause )
		return( 2. * dist - (minsTurn?MinGCost(pos,pos):0.) );
	else
		return dist;
}


template<class state, class action, class environment>
double TIDAStar<state,action,environment>::MinGCost( CRState&, CRState& ) {
	return 1.;
}

#endif
