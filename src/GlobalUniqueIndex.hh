#ifndef GLOBALUNIQUEINDEX_HH_
#define GLOBALUNIQUEINDEX_HH_

// Thanks to Benedikt Oswald and Patrick Leidenberger for sharing their code.

#include <algorithm>
#include <iostream>
#include <map>

#include <dune/common/parallel/mpihelper.hh>
#include <dune/grid/common/datahandleif.hh>

template<class GridView>
class GlobalUniqueIndex
{
private:
  /** define data types */
  typedef typename GridView::Grid Grid;

  typedef typename GridView::Grid::GlobalIdSet         GlobalIdSet;
  typedef typename GridView::Grid::GlobalIdSet::IdType IdType;

  typedef typename GridView::Traits::template Codim<0>::Iterator                                       Iterator;
  typedef typename GridView::template Codim<0>::template Partition<Dune::Interior_Partition>::Iterator ElementIterator;
  typedef typename GridView::Traits::template Codim<0>::Entity                                         Entity;

  typedef std::map<IdType,int> MapId2Index;

private:
  class IndexExchange : public Dune::CommDataHandleIF<IndexExchange, int> {
  public:
    //! returns true if data for this codim should be communicated
    bool contains (int dim, int codim) const {
      return 0 == codim;
    }

    //! returns true if size per entity of given dim and codim is a constant
    bool fixedsize (int dim, int codim) const {
      return true;
    }

    /*! how many objects of type DataType have to be sent for a given entity
     *
     *  Note: Only the sender side needs to know this size. */
    template<class EntityType>
    size_t size (EntityType& e) const {
      return 1;
    }

    /*! pack data from user to message buffer */
    template<class MessageBuffer, class EntityType>
    void gather (MessageBuffer& buff, const EntityType& e) const {
      const IdType id = globalidset_.id(e);

      buff.write(mapid2entity_[id]);
    }

    /*! unpack data from message buffer to user

      n is the number of objects sent by the sender
    */
    template<class MessageBuffer, class EntityType>
    void scatter (MessageBuffer& buff, const EntityType& e, size_t n)
    {
      int x;
      buff.read(x);

      /** only if the incoming index is a valid one,
	  i.e. if it is greater than zero, will it be
	  inserted as the global index; it is made
	  sure in the upper class, i.e. GlobalUniqueIndex,
	  that non-owning processes use -1 to mark an entity
	  that they do not own.
      */
      if(x >= 0) {
	const IdType id = globalidset_.id(e);
	mapid2entity_[id] = x;
      }
    }

    //! constructor
    IndexExchange (const GlobalIdSet& globalidset, MapId2Index& mapid2entity) :
      globalidset_(globalidset),
      mapid2entity_(mapid2entity)
    {}

  private:
    const GlobalIdSet& globalidset_;
    MapId2Index& mapid2entity_;
  };

public:
  /** \brief When the class is instantiated by passing a const. reference to a
   * GridView object, we calculate the complete set of global unique indices
   * so that we can then later query the global index, by directly passing
   * the entity in question, and the respective global index is returned.
   */
  GlobalUniqueIndex(const GridView& gridview) :
    grid_(gridview.grid()),
    gridview_(gridview)
  {
    // Count number of interior elements
    nLocalEntity_ = 0;
    for (ElementIterator eIt = gridview.template begin<0, Dune::Interior_Partition>(); eIt != gridview.template end<0, Dune::Interior_Partition>(); ++eIt)
      nLocalEntity_++;

    // Compute total number of elements
    nGlobalEntity_ = gridview.comm().template sum<int>(nLocalEntity_);

    const int rank_ = gridview.comm().rank();
    const int size_ = gridview.comm().size();

   /** communicate the number of locally owned entities to all other processes so that the respective offset
     *  can be calculated on the respective processor; we use the Dune mpi collective communication facility
     *  for this; first, we gather the number of locally owned entities on the root process and, second, we
     *  broadcast the array to all processes where the respective offset can be calculated. */

    std::vector<int> offset(size_, 0);

    /** Gather number of locally owned entities on root process */
    gridview_.comm().template gather<int>(&nLocalEntity_, offset.data(), 1, 0);

    /** Broadcast the array containing the number of locally owned entities to all processes */
    gridview_.comm().template broadcast<int>(offset.data(), size_, 0);

    indexOffset_.clear();
    indexOffset_.resize(size_, 0);

    for(unsigned int ii = 0; ii < indexOffset_.size(); ++ii)
      for(unsigned int jj = 0; jj < ii; ++jj)
	indexOffset_[ii] += offset[jj];

    /** compute globally unique index over all processes; the idea of the algorithm is as follows: if
     *  an entity is owned by the process, it is assigned an index that is the addition of the offset
     *  specific for this process and a consecutively incremented counter; if the entity is not owned
     *  by the process, it is assigned -1, which signals that this specific entity will get its global
     *  unique index through communication afterwards;
     *
     *  thus, the calculation of the globally unique index is divided into 2 stages:
     *
     *  (1) we calculate the global index independently;
     *
     *  (2) we achieve parallel adjustment by communicating the index
     *      from the owning entity to the non-owning entity.
     *
     */

    /** 1st stage of global index calculation: calculate global index for owned entities */
    globalIndex_.clear();       /** initialize map that stores an entity's global index via it's globally unique id as key */
    const GlobalIdSet& globalIdSet = gridview_.grid().globalIdSet();      /** retrieve globally unique Id set */
    const int myoffset = indexOffset_[rank_];

    int globalcontrib = 0;      /** initialize contribution for the global index */

    for(Iterator iter = gridview_.template begin<0>(); iter!=gridview_.template end<0>(); ++iter) {
      const IdType id = globalIdSet.id((*iter));      /** retrieve the entity's id */

      /** if the entity is owned by the process, go ahead with computing the global index */
      if(iter->partitionType() == Dune::InteriorEntity) {
	const int gindex = myoffset + globalcontrib;    /** compute global index */

	globalIndex_[id] = gindex;                      /** insert pair (key, datum) into the map */
	globalcontrib++;                                /** increment contribution to global index */
      }
      /** if entity is not owned, insert -1 to signal not yet calculated global index */
      else {
	globalIndex_[id] = -1;     /** insert pair (key, datum) into the map */
      }
    }

    /** 2nd stage of global index calculation: communicate global index for non-owned entities */

    /** create the data handle and communicate */
    IndexExchange dh(globalIdSet, globalIndex_);
    gridview_.communicate(dh, Dune::All_All_Interface, Dune::ForwardCommunication);
  }


  /**\brief Given an entity, retrieve its index globally unique over all processes */
  const int globalIndex(const Entity& entity) const
  {
    /** global unique index is only applicable for inter or border type entities */
    const GlobalIdSet& globalIdSet = gridview_.grid().globalIdSet(); /** retrieve globally unique Id set */
    const IdType id = globalIdSet.id(entity);                        /** obtain the entity's id */
    const int gindex = globalIndex_.find(id)->second;                /** retrieve the global index in the map with the id as key */

    return gindex;
  }

  unsigned int nGlobalEntity() {
    return nGlobalEntity_;
  }

  unsigned int nOwnedLocalEntity() {
    return nLocalEntity_;
  }

  const std::vector<int>& indexOffset() {
    return indexOffset_;
  }

protected:
  /** store data members */
  const Grid& grid_;             /** store a const reference to the grid */
  const GridView& gridview_;     /** store a const reference to a gridview */
  int nLocalEntity_;             /** store number of entities that are owned by the local */
  int nGlobalEntity_;            /** store global number of entities, i.e. number of entities without rendundant entities on interprocessor boundaries */
  std::vector<int> indexOffset_; /** store offset of entity index on every process */
  MapId2Index globalIndex_;      /** map stores global index of entities with entity's globally unique id as key */
};

#endif
