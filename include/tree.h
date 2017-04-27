#ifndef tree_h
#define tree_h

#include <vector>

#include "flecsi/topology/tree_topology.h"
#include "flecsi/geometry/point.h"
#include "flecsi/geometry/space_vector.h"

#include "body.h"

using namespace flecsi;

namespace flecsi{
namespace execution{
void specialization_driver(int argc, char * argv[]);
void driver(int argc, char*argv[]); 
} // namespace execution
} // namespace flecsi 

enum locality {LOCAL,NONLOCAL,EXCL,SHARED,GHOST};

struct body_holder_mpi_t{
  static const size_t dimension = gdimension;
  using element_t = type_t; 
  using point_t = flecsi::point<element_t, dimension>;

  point_t position; 
  int owner; 
};

class tree_policy{
public:
  using tree_t = flecsi::topology::tree_topology<tree_policy>;
  using branch_int_t = uint64_t;
  static const size_t dimension = gdimension;
  using element_t = type_t; 
  using point_t = flecsi::point<element_t, dimension>;
  using space_vector_t = flecsi::space_vector<element_t,dimension>;

  class body_holder : 
    public flecsi::topology::tree_entity<branch_int_t,dimension>{
  
  public: 

    body_holder(point_t position,
        body * bodyptr,
        int owner
        )
      :position_(position),bodyptr_(bodyptr),owner_(owner)
    {
      if(bodyptr_==nullptr)
        locality_ = NONLOCAL;
      else
        locality_ = EXCL;
    };

    body_holder()
      :position_(point_t{0,0,0}),
       bodyptr_(nullptr),
       locality_(NONLOCAL),
       owner_(-1)
    {};

    const point_t& coordinates() const {return position_;}
    const point_t& getPosition() const {return position_;}
    body* getBody(){return bodyptr_;};
    int getLocality(){return locality_;};
    int getOwner(){return owner_;};

    void setLocality(locality loc){locality_ = loc;};
    void setBody(body * bodyptr){bodyptr_ = bodyptr;};

    bool
    is_local()
    {
      return locality_ == LOCAL || locality_ == EXCL
        || locality_ == SHARED;
    }

    friend std::ostream& operator<<(std::ostream& os, const body_holder& b){
      os << "Holder: Pos: " <<b.position_ ; 
      if(b.locality_ == LOCAL || b.locality_ == EXCL || b.locality_ == SHARED)
      {
        os<< "LOCAL";
      }else{
        os << "NONLOCAL";
      }
      return os;
    }  

  private:
    point_t position_;
    body * bodyptr_; 
    int locality_;
    int owner_;
  };
    
  using entity_t = body_holder;

  class branch : public flecsi::topology::tree_branch<branch_int_t,dimension>{
  public:
    branch(){}

    void insert(body_holder* ent){
      ents_.push_back(ent);
      if(ents_.size() > (1<<dimension)){
        refine();
      }
    } // insert
    
    auto begin(){
      return ents_.begin();
    }

    auto end(){
      return ents_.end();
    }

    auto clear(){
      ents_.clear();
    }

    void remove(body_holder* ent){
      auto itr = find(ents_.begin(), ents_.end(), ent); 
      assert(itr!=ents_.end());
      ents_.erase(itr);
      if(ents_.empty()){
        coarsen();
      } 
    }

    point_t 
    coordinates(
        const std::array<flecsi::point<element_t, dimension>,2>& range) const{
      point_t p;
      branch_id_t bid = id(); 
      bid.coordinates(range,p);
      return p;
    }

   private:
    std::vector<body_holder*> ents_;
  }; // class branch 

  bool should_coarsen(branch* parent){
    return true;
  }

  using branch_t = branch;

}; // class tree_policy

using tree_topology_t = flecsi::topology::tree_topology<tree_policy>;
using body_holder = tree_topology_t::body_holder;
using point_t = tree_topology_t::point_t;
using branch_t = tree_topology_t::branch_t;
using branch_id_t = tree_topology_t::branch_id_t;
using space_vector_t = tree_topology_t::space_vector_t;

using entity_id_t = flecsi::topology::entity_id_t;

/**
 * Class entity_key_t used to represent the key of a body. 
 * The right way should be to add this informations directly inside 
 * the body_holder. 
 * With this information we should avoid the positions of the holder.
 * This is exactly the same representation as a branch but here 
 * the max depth is always used. 
 */
class entity_key_t
{
  public:
    using int_t = uint64_t;
    static const size_t dimension = gdimension;
    static constexpr size_t bits = sizeof(int_t) * 8;
    static constexpr size_t max_depth = (bits - 1)/dimension;
    using element_t = type_t;

    entity_key_t()
    : id_(0)
    {}

    entity_key_t(
      const std::array<point_t, 2>& range,
      const point_t& p)
    : id_(int_t(1) << ((max_depth * dimension) + ((bits - 1) % dimension)))
    {
      std::array<int_t, dimension> coords;
      for(size_t i = 0; i < dimension; ++i)
      {
        element_t min = range[0][i];
        element_t scale = range[1][i] - min;
        coords[i] = (p[i] - min)/scale * (int_t(1) << (bits - 1)/dimension);
      }
      size_t k = 0;
      for(size_t i = 0; i < max_depth; ++i)
      {
        for(size_t j = 0; j < dimension; ++j)
        {
          int_t bit = (coords[j] & int_t(1) << i) >> i;
          id_ |= bit << (k * dimension + j);
        }
        ++k;
      }
    }

  constexpr entity_key_t(const entity_key_t& bid)
  : id_(bid.id_)
  {}

  static 
  constexpr
  entity_key_t
  null()
  {
    return entity_key_t(0);
  }

  int_t 
  truncate_value(int depth)
  {
    return id_ >> dimension*depth;
  }

  constexpr
  bool 
  is_null() const
  {
    return id_ == int_t(0);
  }

  entity_key_t&
  operator=(
      const entity_key_t& ek
  ) 
  {
    id_ = ek.id_; 
    return *this;
  }

  constexpr
  bool 
  operator==(
      const entity_key_t& ek
  ) const 
  {
    return id_ == ek.id_; 
  }

  constexpr 
  bool 
  operator!=(
    const entity_key_t& ek
  ) const
  {
    return id_ != ek.id_; 
  }

  // Switch representation base on dimension 
  void
  output_(
    std::ostream& ostr
  ) const
  {
    // TODO change for others dimensions
    if(dimension == 3){
      ostr << std::oct << id_ << std::dec;
    }else if(dimension == 1){
      ostr << std::bitset<8>(id_>>bits-8);
    }else{
      ostr << "Dimension not handled";
    }
      //constexpr int_t mask = ((int_t(1) << dimension) - 1) << bits - dimension;
    //size_t d = max_depth;
    //int_t id = id_;
    //
    //while((id & mask) == int_t(0))
    //{
    //  --d;
    //  id <<= dimension;
    //}
    //if(d == 0)
    //{
    //  ostr << "<root>";
    //  return;
    //}
    //id <<= 1 + (bits - 1) % dimension;
    //for(size_t i = 1; i <= d; ++i)
    //{
    //  int_t val = (id & mask) >> (bits - dimension);
    //  ostr << std::oct << val << std::dec; 
    //  //ostr << i << ":" << std::bitset<dimension>(val) << " ";
    //  id <<= dimension;
    //}
  }

  bool
  operator<(
    const entity_key_t& bid
  ) const
  {
    return id_ < bid.id_;
  }
  
  bool
  operator>(
    const entity_key_t& bid
  ) const
  {
    return id_ > bid.id_;
  }

  bool
  operator<=(
    const entity_key_t& bid
  ) const
  {
    return id_ <= bid.id_;
  }
  
  bool
  operator>=(
    const entity_key_t& bid
  ) const
  {
    return id_ >= bid.id_;
  }
  entity_key_t 
  operator/(const int div){
    return entity_key_t(id_/div);
  }

  entity_key_t
  operator+(const entity_key_t& oth )
  {
    return entity_key_t(id_+oth.id_);
  }

  entity_key_t 
  operator-(const entity_key_t& oth)
  {
    return entity_key_t(id_-oth.id_);
  }

  // The first possible key 10000....
  static 
  constexpr
  entity_key_t
  first_key()
  {
    return entity_key_t(int_t(1) << 
        ((max_depth*dimension)+((bits-1)%dimension)));
  }

  // The last key 1777..., should be modified using not bit operation
  static
  constexpr
  entity_key_t
  last_key()
  {
    return entity_key_t(~int_t(0) >> (bits-(1+max_depth)*dimension
          +(gdimension-1)));      
  }

  int_t 
  value()
  {
    return id_;
  }

private:
  int_t id_;
  constexpr
  entity_key_t(
    int_t id
  )
  :id_(id)
  {}
};


#endif // tree_h
