/// HEADER
#include <path_follower/local_planner/scorer.h>

Scorer::Scorer()
{

}

Scorer::~Scorer()
{

}

int Scorer::usUsed(){
    return sw.usElapsedStatic();
}
