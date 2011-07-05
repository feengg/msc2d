#include "critical_point_finder.h"
#include "msc_wrapper.h"
#include "../mesh/Mesh.h"
#include <queue>

namespace msc2d{
using namespace std;
using namespace meshlib;

CPFinder::CPFinder(MscWrapper& _msc): msc(_msc){}
CPFinder::~CPFinder(){}

bool CPFinder::resolveFlatRegion(){
  const vector<double>& sf = msc.sf;
  size_t vert_num = msc.mesh.getVertexNumber();  
  vector<bool> visited_flag(vert_num, false);

  msc.vert_priority_mp.clear();
  for(size_t vid = 0; vid < vert_num; ++vid){
    if(visited_flag[vid] == false){
      queue<int> q;
      q.push(vid);
      visited_flag[vid] = true;

      int priority = -1;
      while(!q.empty()){
        size_t v = q.empty(); q.pop();

        msc.vert_priority_mp[v] = priority++;

        vector<int> adj_vertices = msc.mesh.getAdjVertices(v);
        for(size_t k=0; k<adj_vertices.size(); ++k){
          size_t adj_vid = adj_vertices[k];
          if(!visited_flag[adj_vid] && fabs(sf[vid]-sf[adj_vid]) < LARGE_ZERO_EPSILON){
            q.push(adj_vid);
            visited_flag[adj_vid] = true;
          } // end if
        } // end for
      }//end while      
    } // end if
  }
  assert(msc.vert_priority_mp.size() == vert_num);
  return true;
}


bool CPFinder::findCriticalPoints(){
  if(!resolveFlatRegion()) return false;
  const Mesh& mesh = msc.mesh;
  CriticalPointArray& cp_vec = msc.cp_vec;
  cp_vec.clear();
  for(size_t vid=0; vid<mesh.getVertexNumber(); ++vid){
    if(getPointType(vid) != REGULAR){
      CriticalPoint cp;
      cp.meshIndex = vid;
      cp.type = getPointType(vid);
      cp_vec.push_back(cp);
    }
  }
  return true;
}

CriticalPointType CPFinder::getPointType(int vid) const{  
  vector<int> adj_vertices = msc.mesh.getAdjVertices(vid);
  vector<bool> adj_vflag;

  for(size_t k=0; k<adj_vertices.size(); ++k){
    int _vid = adj_vertices[k];
    if(msc.cmpScalarValue(vid, _vid) == 1) adj_vertices.push_back(false);
    else if(msc.cmpScalarValue(_vid, vid) == -1) adj_vertices.push_back(true);
  }

  if(msc.mesh.isBoundaryVertex(vid)){
    adj_vflag.insert(adj_vflag.end(), adj_vflag.rbegin()+1, adj_vflag.rend()-1);
  }

  int alter_num(0);
  for(size_t k=0; k<adj_vflag.size(); ++k){
    if(adj_vflag[k] != adj_vflag[(k+1)%adj_vflag.size()]) ++alter_num;
  }

  assert(alter_num %2 == 0);
  if(alter_num == 0){
    return msc.sf[adj_vertices[0]] > msc.sf[vid] ? MAXIMAL : MINIMAL;
  }else if(alter_num == 2) return REGULAR;
  else return SADDLE;
}

}// end namespace
