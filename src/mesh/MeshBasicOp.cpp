#include "MeshBasicOp.h"
#include "Mesh.h"
#include "MeshKernel.h"
#include "MeshInfo.h"
#include "../util/utility.h"
#include "../util/heap.h"
#include <queue>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <fstream>
#include <map>
#include <set>
#include <limits>
using namespace std;

namespace meshlib{

MeshBasicOP::MeshBasicOP(Mesh& _mesh) :
    mesh(_mesh),
    vert_vec(mesh.p_Kernel->vert_vec),
    face_vec(mesh.p_Kernel->face_vec),
    edge_vec(mesh.p_Kernel->edge_vec),
    he_vec(mesh.p_Kernel->he_vec){}
MeshBasicOP::~MeshBasicOP() {}

void MeshBasicOP::initModel()
{
  MeshInfo& m_info = *(mesh.p_Info);
  genEdgeInfo();  
  
  genVertAdjacentInfo();
  genEdgeAdjacentInfo();
  genFaceAdjacentInfo();
  
  calFaceNormal();  
  calVertNormal();   
  
  m_info.m_nComponents = countComponentNum();  
  m_info.m_AvgEdgeLength = calAvgEdgeLength(); 
  m_info.m_BoundingBox = calBoundingBox();  
  m_info.m_BoundingSphere = calBoundingSphere();  

  analysisModel(); 
  if(mesh.isManifold()){
    genHalfEdgeDS(); 
  }
  sortAdjacentInfo();  
}

void MeshBasicOP::genEdgeInfo()
{ 

#define MKEDGE(u,v) ((u>v)?make_pair(v,u):make_pair(u,v))  
  map< pair<VertHandle, VertHandle>, EdgeHandle > vert_edge_mp;
  map< pair<VertHandle, VertHandle>, EdgeHandle >::iterator im;

  size_t edge_num = 0;
  for(size_t fid=0; fid<mesh.getFaceNumber(); ++fid){
    const VertHandleArray& vh_vec = face_vec[fid].vert_handle_vec;
    EdgeHandleArray& eh_vec = face_vec[fid].edge_handle_vec;
    eh_vec.clear(); eh_vec.resize(vh_vec.size());
    for(size_t i=0; i<vh_vec.size(); ++i){
      pair<VertHandle, VertHandle> ep = MKEDGE(vh_vec[i], vh_vec[(i+1)%vh_vec.size()]);
      Edge e(vh_vec[i], vh_vec[(i+1)%vh_vec.size()]);
      im = vert_edge_mp.find(ep);
      if(im == vert_edge_mp.end()){
        eh_vec[i] = edge_num;
        vert_edge_mp[ep] = edge_num++;
        edge_vec.push_back(e);
      }else{
        eh_vec[i] = im->second;
      }      
    }
  }
#undef MKEDGE
}

void MeshBasicOP::genVertAdjacentInfo()
{
  size_t vert_num = mesh.getVertexNumber();
  size_t face_num = mesh.getFaceNumber();
  size_t edge_num = mesh.getEdgeNumber();
  vert_adj_vert_vec.clear(); vert_adj_vert_vec.resize(vert_num);
  vert_adj_face_vec.clear(); vert_adj_face_vec.resize(vert_num);
  vert_adj_edge_vec.clear(); vert_adj_edge_vec.resize(vert_num);

  for(size_t fid=0; fid < face_num; ++fid){
    const VertHandleArray& vh_vec = face_vec[fid].vert_handle_vec;
    for(size_t i=0; i<vh_vec.size(); ++i){
      const VertHandle& vh = vh_vec[i];
      vert_adj_face_vec[vh].push_back(fid);
    }
  }

  for(size_t eid=0; eid < edge_num; ++eid){
    const Edge& e = edge_vec[eid];
    vert_adj_vert_vec[e.vert_handle_1].push_back(e.vert_handle_2);
    vert_adj_vert_vec[e.vert_handle_2].push_back(e.vert_handle_1);
    vert_adj_edge_vec[e.vert_handle_1].push_back(eid);
    vert_adj_edge_vec[e.vert_handle_2].push_back(eid);
  }  
}

void MeshBasicOP::genEdgeAdjacentInfo()
{
  size_t edge_num = mesh.getEdgeNumber();
  size_t face_num = mesh.getFaceNumber();

  edge_adj_face_vec.clear();
  edge_adj_face_vec.resize(edge_num);

  for(size_t fid = 0; fid < face_num; ++fid){
    const Face& f = (mesh.p_Kernel->face_vec)[fid];
    for(size_t i=0; i<f.edge_handle_vec.size(); ++i){
      EdgeHandle eh = f.edge_handle_vec[i];
      edge_adj_face_vec[eh].push_back(fid);
    }
  }
}

void MeshBasicOP::genFaceAdjacentInfo()
{
  size_t face_num = mesh.getFaceNumber();

  face_adj_face_vec.clear();
  face_adj_face_vec.resize(face_num);
  for(size_t fid = 0; fid < face_num; ++fid){
    const Face& f = (mesh.p_Kernel->face_vec)[fid];
    for(size_t i=0; i<f.edge_handle_vec.size(); ++i){
      EdgeHandle eh = f.edge_handle_vec[i];
      const FaceHandleArray& fh_vec = edge_adj_face_vec[eh];
      for(size_t j=0; j<fh_vec.size(); ++j){
        if(fh_vec[j] != fid) face_adj_face_vec[fid].push_back(fh_vec[j]);
      }
    }
  }
}

const VertHandleArray& MeshBasicOP::getAdjVertArray(const VertHandle& vh) const
{
  return vert_adj_vert_vec[vh];
}

const VertHandleArray& MeshBasicOP::getAdjFaceArray(const FaceHandle& vh) const
{
  return vert_adj_face_vec[vh];
}

EdgeHandle MeshBasicOP::getEdgeHandle(VertHandle vh1, VertHandle vh2) const
{
  if(vh1 == vh2) return -1;
  const EdgeHandleArray& eh_vec1 = vert_adj_edge_vec[vh1];
  const EdgeHandleArray& eh_vec2 = vert_adj_edge_vec[vh2];
  for(size_t k=0; k<eh_vec1.size(); ++k){
    const EdgeHandle& eh = eh_vec1[k];
    if(find(eh_vec2.begin(), eh_vec2.end(), eh) != eh_vec2.end()) return eh;
  }
  return -1;
}

HalfEdgeHandle MeshBasicOP::getHalfEdgeHandle(VertHandle vh1, VertHandle vh2) const{
  EdgeHandle eh = getEdgeHandle(vh1, vh2);
  const Edge& e = edge_vec[eh];
  if(e.he_handle_1 != -1){
    const HalfEdge& curr_he = he_vec[e.he_handle_1];
    const HalfEdge& next_he = he_vec[curr_he.next_he_handle];
    if(curr_he.vert_handle == vh1 && next_he.vert_handle == vh2) return e.he_handle_1;
  }
  if(e.he_handle_2 != -1){
    const HalfEdge& curr_he = he_vec[e.he_handle_2];
    const HalfEdge& next_he = he_vec[curr_he.next_he_handle];
    if(curr_he.vert_handle == vh1 && next_he.vert_handle == vh2) return e.he_handle_2;
  }
  return -1;
}
    
BoundingBox MeshBasicOP::calBoundingBox() const
{
  BoundingBox bb;
  const vector<Vert>& vert_vec = mesh.p_Kernel->getVertArray();
  if(vert_vec.size() == 0) return bb;
  bb.box_min = bb.box_max = vert_vec[0].coord; 
  for(VertHandle vh = 0; vh < vert_vec.size(); ++vh){
    const Coord3D& coord = vert_vec[vh].coord;
    for(size_t k=0; k<3; ++k){
      bb.box_min[k] = min(bb.box_min[k], coord[k]);
      bb.box_max[k] = max(bb.box_max[k], coord[k]);
    }
  }
  bb.box_dim = bb.box_max - bb.box_min;
  return bb;
}   

BoundingSphere MeshBasicOP::calBoundingSphere() const
{
  BoundingSphere bs;
  const vector<Vert>& vert_vec = mesh.p_Kernel->getVertArray();
  if(vert_vec.size() == 0) return bs;
  bs.center.setVec3Ds(0, 0, 0); 
  for(VertHandle vh = 0; vh < vert_vec.size(); ++vh){
    bs.center += vert_vec[vh].coord;
  }
  bs.center /= vert_vec.size();
  bs.radius = (vert_vec[0].coord - bs.center).abs();
  for(VertHandle vh = 0; vh < vert_vec.size(); ++vh){
    bs.radius = std::max(bs.radius, (vert_vec[vh].coord - bs.center).abs());
  }
  return bs;
}

void MeshBasicOP::calFaceNormal()
{
  const VertArray& vert_vec = mesh.p_Kernel->getVertArray();
  FaceArray& face_vec = mesh.p_Kernel->getFaceArray();

  for(FaceHandle fh = 0; fh < face_vec.size(); ++fh){
    Face& face = face_vec[fh];
    const vector<VertHandle>& vh_vec = face.vert_handle_vec;
    const Vert& v0 = vert_vec[vh_vec[0]];
    const Vert& v1 = vert_vec[vh_vec[1]];
    const Vert& v2 = vert_vec[vh_vec[2]];
    face.normal = cross(v1.coord - v0.coord, v2.coord - v0.coord);
    if(!face.normal.normalize()) face.normal = COORD_AXIS_Z;
  }
}

void MeshBasicOP::calVertNormal()
{
  vector<Vert>& vert_vec = mesh.p_Kernel->getVertArray();
  const vector<Face>& face_vec = mesh.p_Kernel->getFaceArray();

  for(VertHandle vh = 0; vh < vert_vec.size(); ++vh){
    Vert& vert = vert_vec[vh];
    vert.normal.setVec3Ds(0, 0, 0);
    const vector<FaceHandle>& adj_faces = getAdjFaceArray(vh);
    for(size_t k=0; k<adj_faces.size(); ++k){
      const Face& face = face_vec[adj_faces[k]];
      vert.normal += face.normal;
    }
    if(!vert.normal.normalize()) vert.normal = COORD_AXIS_Z;
  }
}

size_t MeshBasicOP::countComponentNum() const
{
  size_t vert_num = mesh.getVertexNumber();

  const vector<Vert>& vert_vec = mesh.p_Kernel->getVertArray();
  size_t component_num = 0;
  std::vector<bool> visited_flag(vert_num, false);

  for(VertHandle vh = 0; vh < vert_vec.size(); ++vh){
    if(!visited_flag[vh]){
      component_num ++;
      queue<VertHandle> q;
      q.push(vh); visited_flag[vh] = true;
      while(!q.empty()){
        VertHandle _vh = q.front(); q.pop();
        const vector<VertHandle>& adj_vert = getAdjVertArray(_vh);
        for(size_t k=0; k<adj_vert.size(); ++k){
          if(!visited_flag[adj_vert[k]] ){
            q.push(adj_vert[k]); visited_flag[adj_vert[k]] = true;
          }
        }
      }
    }
  }
  return component_num;
}

double MeshBasicOP::calAvgEdgeLength() const
{
  const vector<Edge>& edge_vec = mesh.p_Kernel->getEdgeArray();
  const vector<Vert>& vert_vec = mesh.p_Kernel->getVertArray();

  double sum_len = 0;
  size_t edge_num = edge_vec.size();
  for(size_t k=0; k<edge_num; ++k){
    const Edge& edge = edge_vec[k];
    const Vert& vert1 = vert_vec[edge.vert_handle_1];
    const Vert& vert2 = vert_vec[edge.vert_handle_2];

    double edge_len = (vert1.coord - vert2.coord).abs();
    sum_len += edge_len;
  }
  return sum_len / edge_num*1.0;
}

void MeshBasicOP::analysisModel()
{  
  bool tri_mesh(true), quad_mesh(false), poly_mesh(false), manifold(true);

  for(size_t eid=0; eid<mesh.getEdgeNumber(); ++eid){
    Edge& e = (mesh.p_Kernel->edge_vec)[eid];
    Vert& v1 = (mesh.p_Kernel->vert_vec)[e.vert_handle_1];
    Vert& v2 = (mesh.p_Kernel->vert_vec)[e.vert_handle_2];

    FaceHandleArray& fh_vec = edge_adj_face_vec[eid];
    if(fh_vec.size() == 1){
      Util::SetFlag(e.flag, BOUNDARY_EDGE);
      Util::SetFlag(v1.flag, BOUNDARY_VERT);
      Util::SetFlag(v2.flag, BOUNDARY_VERT);
    }else if(fh_vec.size() == 0 || fh_vec.size() > 2){
      manifold = false;
      Util::SetFlag(e.flag, NONMANIFOLD_EDGE);
    }    
  }
  
  for(size_t vid=0; vid<mesh.getVertexNumber(); ++vid){
    Vert& v = (mesh.p_Kernel->vert_vec)[vid];
    VertHandleArray& vh_vec = vert_adj_vert_vec[vid];    
    if(vh_vec.size() == 0){
      Util::SetFlag(v.flag, ISOLATED_VERT);
    }else if(vh_vec.size() == 1){
      manifold = false;
      Util::SetFlag(v.flag, NONMANIFOLD_VERT);
    }

    EdgeHandleArray& eh_vec = vert_adj_edge_vec[vid];
    int bdy_edge_num(0);
    for(size_t k=0; k<eh_vec.size(); ++k){
      if(mesh.isBoundaryEdge(eh_vec[k])) ++bdy_edge_num;        
    }
    if(bdy_edge_num > 2){
      manifold = false;
      Util::SetFlag(v.flag, NONMANIFOLD_VERT);
    }
  }
  
  //! analysis faces
  FaceArray& face_vec = mesh.p_Kernel->getFaceArray();
  for(size_t k=0; k<face_vec.size(); ++k){
    Face& face = face_vec[k];
    size_t vert_num = face.vert_handle_vec.size();
    if(vert_num < 3) {
      manifold = false;
      Util::SetFlag(face.flag, NONMANIFOLD_FACE);
    }            
    if(vert_num !=3 && vert_num != 4){
      poly_mesh = true; tri_mesh = quad_mesh = false;
    }
    if(vert_num == 4){
      if(!poly_mesh) { quad_mesh = true; tri_mesh = false;}
    }
    for(size_t i=0; i<face.vert_handle_vec.size(); ++i){
      VertHandle cur_handle = face.vert_handle_vec[i];
      VertHandle nxt_handle = face.vert_handle_vec[(i+1)%vert_num];

      if(cur_handle == nxt_handle){
        manifold = false;
        Util::SetFlag(face.flag, NONMANIFOLD_FACE);
        continue;
      }
    }

    for(size_t i=0; i<face.edge_handle_vec.size(); ++i){
      EdgeHandle eh = face.edge_handle_vec[i];
      Edge& e = (mesh.p_Kernel->edge_vec)[eh];
      if(Util::IsSetFlag(e.flag, BOUNDARY_EDGE)) Util::SetFlag(face.flag, BOUNDARY_FACE);      
      if(Util::IsSetFlag(e.flag, NONMANIFOLD_EDGE)) Util::SetFlag(face.flag, NONMANIFOLD_FACE);
    }
  }
        
  // set mesh flag
  MeshInfo& mesh_info = *(mesh.p_Info);
  if(tri_mesh) Util::SetFlag(mesh_info.flag, TRIMESH);
  if(quad_mesh) Util::SetFlag(mesh_info.flag, QUADMESH);
  if(poly_mesh) Util::SetFlag(mesh_info.flag, POLYMESH);
  if(manifold) Util::SetFlag(mesh_info.flag, MANIFOLD);
}

void MeshBasicOP::sortAdjacentInfo()
{
  /// make sure each vertex's 1-ring neighbors to be CCW
  for(size_t vid=0; vid<mesh.getVertexNumber(); ++vid){
    const Vert& v = (mesh.p_Kernel->vert_vec)[vid];
    /// only sort for manifold vertex
    if(Util::IsSetFlag(v.flag, NONMANIFOLD_VERT)) continue;
    if(Util::IsSetFlag(v.flag, ISOLATED_VERT)) continue;    
    
    FaceHandleArray fh_vec_bak = vert_adj_face_vec[vid];
    EdgeHandleArray eh_vec_bak = vert_adj_edge_vec[vid];
    VertHandleArray vh_vec_bak = vert_adj_vert_vec[vid];
    
    FaceHandleArray& adj_faces = vert_adj_face_vec[vid];
    EdgeHandleArray& adj_edges = vert_adj_edge_vec[vid];
    VertHandleArray& adj_verts = vert_adj_vert_vec[vid];

    size_t adj_num = fh_vec_bak.size();
    if(adj_num == 1){
      const Face& f = (mesh.p_Kernel->face_vec)[fh_vec_bak[0]];
      const VertHandleArray& vh_vec = f.vert_handle_vec;
      const EdgeHandleArray& eh_vec = f.edge_handle_vec;
      size_t idx = distance(vh_vec.begin(), find(vh_vec.begin(), vh_vec.end(), vid));
      assert(idx != vh_vec.size());
      assert(adj_edges.size() == 2 && adj_verts.size() == 2);
      VertHandle prev_vid = vh_vec[(idx+vh_vec.size()-1)%vh_vec.size()];
      VertHandle next_vid = vh_vec[(idx+1)%vh_vec.size()];
      EdgeHandle prev_eid = eh_vec[(idx+eh_vec.size()-1)%eh_vec.size()];
      EdgeHandle next_eid = eh_vec[idx];
      adj_verts[0] = next_vid; adj_verts[1] = prev_vid;
      adj_edges[0] = next_eid; adj_edges[1] = prev_eid;
      continue;
    }
    
    size_t next_idx = adj_num;
    VertHandle next_vid;
    EdgeHandle next_eid;

    if(Util::IsSetFlag(v.flag, BOUNDARY_VERT)){
      /// make sure the first face of boundary vertex is a boundary face
      for(size_t k=0; k<adj_num; ++k){
        const Face& f = (mesh.p_Kernel->face_vec)[fh_vec_bak[k]];
        const VertHandleArray& vh_vec = f.vert_handle_vec;
        const EdgeHandleArray& eh_vec = f.edge_handle_vec;
        if(Util::IsSetFlag(f.flag, BOUNDARY_FACE)){
          size_t idx = distance(vh_vec.begin(), find(vh_vec.begin(), vh_vec.end(), vid));
          assert(idx != vh_vec.size());
          VertHandle prev_vid = vh_vec[ (idx+vh_vec.size()-1) % vh_vec.size()];

          bool flag = false;
          for(size_t j=0; j<adj_num; ++j){ if(j == k) continue;
            const Face& f = (mesh.p_Kernel->face_vec)[fh_vec_bak[j]];
            const VertHandleArray& vh_vec = f.vert_handle_vec;
            if(find(vh_vec.begin(), vh_vec.end(), prev_vid) != vh_vec.end()){
              flag = true; break;
            }
          }
          if(flag == true) { // find the first face 
            next_vid = vh_vec[(idx+1)%vh_vec.size()];
            next_eid = eh_vec[idx];
            next_idx = k;  break;
          }
        }// end if
      } // end for
    }else{
      next_idx = 0;
      const Face& f = (mesh.p_Kernel->face_vec)[fh_vec_bak[next_idx]];
      const VertHandleArray& vh_vec = f.vert_handle_vec;
      const EdgeHandleArray& eh_vec = f.edge_handle_vec;
      size_t idx = distance(vh_vec.begin(), find(vh_vec.begin(), vh_vec.end(), vid));
      assert(idx != vh_vec.size());
      next_vid = vh_vec[(idx+1)%vh_vec.size()]; next_eid = eh_vec[idx];
    }
    assert(next_idx != adj_num);

    adj_faces[0] = fh_vec_bak[next_idx];
    adj_verts[0] = next_vid; adj_edges[0] = next_eid;
    
    for(size_t k=1; k<adj_num; ++k){
      FaceHandle fh = fh_vec_bak[next_idx];
      const Face& f = (mesh.p_Kernel->face_vec)[fh];
      const VertHandleArray& vh_vec = f.vert_handle_vec;
      const EdgeHandleArray& eh_vec = f.edge_handle_vec;
      size_t idx = distance(vh_vec.begin(), find(vh_vec.begin(), vh_vec.end(), vid));
      assert(idx != vh_vec.size());
      VertHandle prev_vid = vh_vec[ (idx+vh_vec.size()-1) % vh_vec.size()];
      for(size_t j=0; j<adj_num; ++j){
        if(j==next_idx) continue;
        const Face& adj_f = (mesh.p_Kernel->face_vec)[fh_vec_bak[j]];
        const VertHandleArray& vh_vec = adj_f.vert_handle_vec;
        if(find(vh_vec.begin(), vh_vec.end(), prev_vid) != vh_vec.end()){
          next_idx = j;
          next_vid = prev_vid;
          next_eid = eh_vec[(idx+eh_vec.size()-1)%eh_vec.size()];
          break;
        }
      }       
      assert(fh_vec_bak[next_idx] != adj_faces[k-1]);
      adj_faces[k] = fh_vec_bak[next_idx];
      adj_verts[k] = next_vid; adj_edges[k] = next_eid;
    }    
    if(Util::IsSetFlag(v.flag, BOUNDARY_VERT)){// add the last vertex/edge
      for(size_t k=0; k<vh_vec_bak.size(); ++k){
        if(find(adj_verts.begin(), adj_verts.end(), vh_vec_bak[k]) == adj_verts.end()){
          adj_verts[adj_verts.size()-1] = vh_vec_bak[k]; break;
        }
      }
      for(size_t k=0; k<eh_vec_bak.size(); ++k){
        if(find(adj_edges.begin(), adj_edges.end(), eh_vec_bak[k]) == adj_edges.end()){
          adj_edges[adj_edges.size()-1] = eh_vec_bak[k]; break;
        }
      }
    }
  }
  
}

void MeshBasicOP::genHalfEdgeDS()
{
  if(mesh.isManifold() == false){
    cerr << "Error: cannot generate halfedge, non-manifold mesh" << endl;
    return;
  }

  map <EdgeHandle, HalfEdgeHandle> edge_map;

  FaceArray& face_vec = mesh.p_Kernel->getFaceArray();
  HalfEdgeArray& he_vec = mesh.p_Kernel->getHEArray();
  for(size_t fid=0; fid<face_vec.size(); ++fid){
    const Face& face = face_vec[fid];
    const VertHandleArray& vh_vec = face.vert_handle_vec;
    const EdgeHandleArray& eh_vec = face.edge_handle_vec;
    HalfEdgeHandle origin_he_handle = he_vec.size();
    size_t vh_num = vh_vec.size();
    for(size_t k=0; k<vh_vec.size(); ++k){
      /// create a new halfedge
      HalfEdgeHandle curr_he_handle = origin_he_handle + k;
      HalfEdgeHandle next_he_handle = origin_he_handle + (k+1)%vh_num;
      HalfEdgeHandle prev_he_handle = origin_he_handle + (k+vh_num-1)%vh_num;
      HalfEdgeHandle oppo_he_handle = -1;

      Edge& curr_edge = edge_vec[eh_vec[k]];
      if(edge_map.find(eh_vec[k]) != edge_map.end()){ /// update info
        oppo_he_handle = edge_map[eh_vec[k]];
        HalfEdge& oppo_he = he_vec[oppo_he_handle];
        oppo_he.oppo_he_handle = curr_he_handle;
        curr_edge.he_handle_2 = curr_he_handle;
      }else{
        curr_edge.he_handle_1 = curr_he_handle;
        edge_map.insert(make_pair(eh_vec[k], curr_he_handle));
      }
      he_vec.push_back(HalfEdge(vh_vec[k], eh_vec[k], fid,
                                prev_he_handle, next_he_handle, oppo_he_handle));
    }
  }
  /// form outer boundary halfedge
  vector<HalfEdgeHandle> bd_he_vec;
  for(size_t k=0; k<he_vec.size(); ++k){
    if(he_vec[k].oppo_he_handle == -1){
      HalfEdge& inner_he = he_vec[k];
      HalfEdgeHandle prev_he_handle = inner_he.prev_he_handle;
      HalfEdgeHandle next_he_handle = inner_he.next_he_handle;

      /// find previous out halfedge for this outer halfedge
      HalfEdgeHandle curr_he_handle = k;
      while(he_vec[prev_he_handle].oppo_he_handle != -1){
        HalfEdge prev_he = he_vec[prev_he_handle];
        if(he_vec[prev_he.oppo_he_handle].face_handle == -1) break;
        curr_he_handle = he_vec[prev_he_handle].oppo_he_handle;
        prev_he_handle = he_vec[curr_he_handle].prev_he_handle;
      }

      /// find next out halfedge for this outer halfedge
      curr_he_handle = k;
      while(he_vec[next_he_handle].oppo_he_handle != -1){
        HalfEdge next_he = he_vec[next_he_handle];
        if(he_vec[next_he.oppo_he_handle].face_handle == -1) break;
        curr_he_handle = he_vec[next_he_handle].oppo_he_handle;
        next_he_handle = he_vec[curr_he_handle].next_he_handle;
      }

      he_vec.push_back(HalfEdge(he_vec[inner_he.next_he_handle].vert_handle, inner_he.edge_handle,
                              -1, next_he_handle, prev_he_handle, k));
      inner_he.oppo_he_handle = he_vec.size()-1;
      Edge& e = edge_vec[inner_he.edge_handle];
      assert(e.he_handle_1 != -1 && e.he_handle_2 == -1);
      e.he_handle_2 = he_vec.size()-1;
      bd_he_vec.push_back(he_vec.size()-1);
    }
  }

  // get boundary he's prev/next he
  for(size_t k=0; k<bd_he_vec.size(); ++k){
    HalfEdge& he = he_vec[bd_he_vec[k]];
    he.prev_he_handle = he_vec[he.prev_he_handle].oppo_he_handle;
    he.next_he_handle = he_vec[he.next_he_handle].oppo_he_handle;
  }

  //! generate face half edge info
  for(size_t k=0; k<face_vec.size(); ++k){
    Face& f = face_vec[k];
    const EdgeHandleArray& eh_vec = f.edge_handle_vec;
    for(size_t i=0; i<eh_vec.size(); ++i){
      const Edge& e = edge_vec[eh_vec[i]];
      const HalfEdge& he = he_vec[e.he_handle_1];
      if(he.face_handle == k) f.he_handle_vec.push_back(e.he_handle_1);
      else{
        const HalfEdge& _he = he_vec[e.he_handle_2];
        if(_he.face_handle == k) f.he_handle_vec.push_back(e.he_handle_2);
      }
    }
  }
}

bool MeshBasicOP::getInnerFaces(const PATH& loop, FaceHandleArray& fh_vec) const{
  fh_vec.clear();
  if(loop.size()<3 || loop[0] != loop[loop.size()-1]) return false;
  for(size_t k=0; k<loop.size(); ++k) {
    const Vert& vert = vert_vec[loop[k]];
    if(Util::IsSetFlag(vert.flag, NONMANIFOLD_VERT)) return false;
  }

  set<HalfEdgeHandle> bd_edge_set;
  for(size_t k=0; k<loop.size()-1; ++k) {
    HalfEdgeHandle hh = getHalfEdgeHandle(loop[k], loop[k+1]);
    if(hh == -1){
      cerr << "Not a close loop" << endl;
      return false;
    }
    bd_edge_set.insert(hh);
  }
  
  set<FaceHandle> faces;
  queue<FaceHandle> q;

  for(size_t k=0; k<loop.size()-1; ++k){
    HalfEdgeHandle hh = getHalfEdgeHandle(loop[k], loop[k+1]);
    HalfEdgeHandle oppo_hh = he_vec[hh].oppo_he_handle;
    if(bd_edge_set.find(hh) != bd_edge_set.end() &&
       bd_edge_set.find(oppo_hh) != bd_edge_set.end()) continue;
    const HalfEdge& he = he_vec[hh];
    FaceHandle fh = he.face_handle;
    if(fh == -1) return false;
    if(faces.find(fh) == faces.end()){
      q.push(fh); faces.insert(fh);
      while(!q.empty()){
        FaceHandle fh = q.front(); q.pop();
        const HalfEdgeHandleArray& hh_vec = face_vec[fh].he_handle_vec;
        for(size_t k=0; k<hh_vec.size(); ++k){
          if(bd_edge_set.find(hh_vec[k]) != bd_edge_set.end()) continue;
          HalfEdgeHandle oppo_hh = he_vec[hh_vec[k]].oppo_he_handle;
          FaceHandle fh = he_vec[oppo_hh].face_handle;
          if(fh != -1 && faces.find(fh) == faces.end()) {
            faces.insert(fh);
            q.push(fh);
          }
        }
      }// end while
    }
  }

  fh_vec.clear(); fh_vec.resize(faces.size());
  fh_vec.assign(faces.begin(), faces.end());
  return true;
}

bool MeshBasicOP::getShortestPath(VertHandle start, VertHandle end,
                                  PATH &path, const std::set<EdgeHandle> &edge_set) const{
  path.clear();
  if(start == end) path.push_back(start);

#define INIFINITE_DIST (numeric_limits<double>::infinity())

  map<int, double> vert_dist;
  map<int, double>::iterator iter;
  map<int, int> prev_vert;
  prev_vert[end] = -1;
  CHeap heap;
  vert_dist[end] = 0.0;

  Node* pNode = new Node;
  pNode->v = 0.0;
  pNode->type = end;
  heap.insert(pNode);

  bool flag = true;

  while(!heap.heapEmpty()){
    Node* pNode = heap.Remove();
    VertHandle vh = pNode->type;
    double vdist = vert_dist[vh];
    const Coord& vc = mesh.getVertexCoord(vh);
    const VertHandleArray& adj_vertices = vert_adj_vert_vec[vh];
    for(size_t i=0; i<adj_vertices.size(); ++i){
      VertHandle adj_vh = adj_vertices[i];
      EdgeHandle eh = getEdgeHandle(vh, adj_vh);
      if(edge_set.find(eh) == edge_set.end()) continue;
      const Coord& _vc = mesh.getVertexCoord(adj_vertices[i]);
      double edge_len = (vc-_vc).abs();
      double adj_vdist = INIFINITE_DIST;
      if( (iter = vert_dist.find(adj_vh)) != vert_dist.end()) adj_vdist = iter->second;
      if(vdist + edge_len < adj_vdist){
        vert_dist[adj_vh] = vdist + edge_len;
        prev_vert[adj_vh] = vh;
        int index = heap.heapFind(adj_vh);
        if(index !=0){
          heap.a[index]->v = vdist + edge_len;
          heap.upheap(index);
        }else{
          Node* pNewNode = new Node;
          pNewNode->v = vdist + edge_len;
          pNewNode->type = adj_vh;
          heap.insert(pNewNode);
        }
      }
    }
    delete pNode;
    if(vh == start) { flag = true; break; }
  }

  while(!heap.heapEmpty()){
    Node* pNode = heap.Remove();
    delete pNode;
  }

  if(!flag) return false;

  int curr_vid = start;
  while(prev_vert[curr_vid] != -1){
    path.push_back(curr_vid);
    curr_vid = prev_vert[curr_vid];
  }
  path.push_back(end);
  return true;
}

#undef INIFINITE_DIST

}
