// -*- C++ -*-
//
// Package:    TrackIPProducer
// Class:      TrackIPProducer
// 
/**\class TrackIPProducer TrackIPProducer.cc RecoBTau/TrackIPProducer/src/TrackIPProducer.cc

 Description: <one line class summary>

 Implementation:
     <Notes on implementation>
*/
//
// Original Author:  Andrea Rizzi
//         Created:  Thu Apr  6 09:56:23 CEST 2006
// $Id: TrackIPProducer.cc,v 1.4 2007/05/23 14:41:16 arizzi Exp $
//
//


// system include files
#include <memory>

// user include files
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "RecoBTag/ImpactParameter/interface/TrackIPProducer.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"

#include "FWCore/ParameterSet/interface/ParameterSet.h"

#include "DataFormats/BTauReco/interface/JetTag.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/BTauReco/interface/TrackIPTagInfoFwd.h"
#include "DataFormats/BTauReco/interface/TrackIPTagInfo.h"
#include "DataFormats/BTauReco/interface/JetTracksAssociation.h"

#include "TrackingTools/TransientTrack/interface/TransientTrackBuilder.h"
#include "TrackingTools/IPTools/interface/IPTools.h"
#include "TrackingTools/Records/interface/TransientTrackRecord.h"

#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/ESHandle.h"
//#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "RecoBTag/TrackProbability/interface/HistogramProbabilityEstimator.h"

#include <iostream>

using namespace std;
using namespace reco;
using namespace edm;

//
// constructors and destructor
//
TrackIPProducer::TrackIPProducer(const edm::ParameterSet& iConfig) : 
  m_config(iConfig),m_probabilityEstimator(0)  {

  m_useDB=iConfig.getParameter<bool>("useDB");
  if(!m_useDB)
  {
   edm::FileInPath f2d("RecoBTag/TrackProbability/data/2DHisto.xml");
   edm::FileInPath f3d("RecoBTag/TrackProbability/data/3DHisto.xml");
   m_probabilityEstimator=new HistogramProbabilityEstimator( new AlgorithmCalibration<TrackClassFilterCategory,CalibratedHistogramXML>((f3d.fullPath()).c_str()),
               new AlgorithmCalibration<TrackClassFilterCategory,CalibratedHistogramXML>((f2d.fullPath()).c_str())) ;
  }
  m_calibrationCacheId3D= 0;
  m_calibrationCacheId2D= 0;
  
  m_associator = m_config.getParameter<string>("jetTracks");
  m_primaryVertexProducer = m_config.getParameter<string>("primaryVertex");

  m_computeProbabilities = m_config.getParameter<bool>("computeProbabilities"); //FIXME: use or remove
  
  m_cutPixelHits     =  m_config.getParameter<int>("minimumNumberOfPixelHits"); //FIXME: use or remove
  m_cutTotalHits     =  m_config.getParameter<int>("minimumNumberOfHits"); // used
  m_cutMaxTIP        =  m_config.getParameter<double>("maximumTransverseImpactParameter"); // used
  m_cutMinPt         =  m_config.getParameter<double>("minimumTransverseMomentum"); // used
  m_cutMaxDecayLen   =  m_config.getParameter<double>("maximumDecayLength"); //used
  m_cutMaxChiSquared =  m_config.getParameter<double>("maximumChiSquared"); //used
  m_cutMaxLIP        =  m_config.getParameter<double>("maximumLongitudinalImpactParameter"); //used
  m_cutMaxDistToAxis =  m_config.getParameter<double>("maximumDistanceToJetAxis"); //used
  m_directionWithTracks =  m_config.getParameter<bool>("jetDirectionUsingTracks"); //used


   produces<reco::TrackIPTagInfoCollection>();

}

TrackIPProducer::~TrackIPProducer()
{
 delete m_probabilityEstimator;
}

//
// member functions
//
// ------------ method called to produce the data  ------------
void
TrackIPProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup)
{
   using namespace edm;
   
   if(m_computeProbabilities && m_useDB) checkEventSetup(iSetup); //Update probability estimator if event setup is changed
 
  //input objects 
   Handle<reco::JetTracksAssociationCollection> jetTracksAssociation;
   iEvent.getByLabel(m_associator,jetTracksAssociation);
   
   Handle<reco::VertexCollection> primaryVertex;
   iEvent.getByLabel(m_primaryVertexProducer,primaryVertex);
   
   edm::ESHandle<TransientTrackBuilder> builder;
   iSetup.get<TransientTrackRecord>().get("TransientTrackBuilder",builder);
  //  m_algo.setTransientTrackBuilder(builder.product());

  

   //output collections 
   reco::TrackIPTagInfoCollection * outCollection = new reco::TrackIPTagInfoCollection();

   //use first pv of the collection
   //FIXME: use BeamSpot when pv is missing
   const  Vertex  *pv;
   edm::Ref<VertexCollection> * pvRef;
   bool pvFound = (primaryVertex->size() != 0);
   if(pvFound)
   {
    pv = &(*primaryVertex->begin());
    pvRef = new edm::Ref<VertexCollection>(primaryVertex,0); // we always use the first vertex at the moment
   }
    else 
   { // create a dummy PV
     Vertex::Error e;
     e(0,0)=0.0015*0.0015;
      e(1,1)=0.0015*0.0015;
     e(2,2)=15.*15.;
     Vertex::Point p(0,0,0);
     pv=  new Vertex(p,e,1,1,1);
     pvRef = new edm::Ref<VertexCollection>();
   }
   
   double pvZ=pv->z();
 
   int i=0;
   JetTracksAssociationCollection::const_iterator it = jetTracksAssociation->begin();
   for(; it != jetTracksAssociation->end(); it++, i++)
     {
        TrackRefVector tracks = it->second;
        math::XYZVector jetMomentum;
        if(m_directionWithTracks) 
         {
           for (TrackRefVector::const_iterator itTrack = tracks.begin(); itTrack != tracks.end(); ++itTrack) {
              jetMomentum+=(**itTrack).innerMomentum();
             }
         }
          else
         {
            jetMomentum=it->first->momentum();
         } 
        GlobalVector direction(jetMomentum.x(),jetMomentum.y(),jetMomentum.z());
        
        TrackRefVector selectedTracks;
        vector<Measurement1D> ip3Dv,ip2Dv,dLenv,jetDistv;
        vector<float> prob2D,prob3D;
       for (TrackRefVector::const_iterator itTrack = tracks.begin(); itTrack != tracks.end(); ++itTrack) {
             const Track & track = **itTrack;
             const TransientTrack & transientTrack = builder->build(&(**itTrack));
             //FIXME: this stuff is computed twice. transienttrack like container in IPTools for caching? 
             //       is it needed? does it matter at HLT?
             float distToAxis = IPTools::jetTrackDistance(transientTrack,direction,*pv).second.value();
             float dLen = IPTools::signedDecayLength3D(transientTrack,direction,*pv).second.value();

         if( track.pt() > m_cutMinPt  &&                          // minimum pt
                 fabs(track.d0()) < m_cutMaxTIP &&                // max transverse i.p.
                 track.recHitsSize() >= m_cutTotalHits &&         // min num tracker hits
                 fabs(track.dz()-pvZ) < m_cutMaxLIP &&            // z-impact parameter
                 track.normalizedChi2() < m_cutMaxChiSquared &&   // normalized chi2
                 fabs(distToAxis) < m_cutMaxDistToAxis  &&        // distance to JetAxis
                 fabs(dLen) < m_cutMaxDecayLen &&                 // max decay len
                 track.hitPattern().numberOfValidPixelHits() >= m_cutPixelHits //min # pix hits 
           )     // quality cuts
        { 
         //Fill vectors
         selectedTracks.push_back(*itTrack);
         ip3Dv.push_back(IPTools::signedImpactParameter3D(transientTrack,direction,*pv).second);
         ip2Dv.push_back(IPTools::signedTransverseImpactParameter(transientTrack,direction,*pv).second);
         dLenv.push_back(IPTools::signedDecayLength3D(transientTrack,direction,*pv).second);
         jetDistv.push_back(IPTools::jetTrackDistance(transientTrack,direction,*pv).second);
         if(m_computeProbabilities) {
              //probability with 3D ip
              pair<bool,double> probability =  m_probabilityEstimator->probability(0,ip3Dv.back().significance(),track,*(it->first),*pv);
              if(probability.first)  prob3D.push_back(probability.second); else  prob3D.push_back(-1.); 
              
              //probability with 2D ip
              probability =  m_probabilityEstimator->probability(1,ip2Dv.back().significance(),track,*(it->first),*pv);
              if(probability.first)  prob2D.push_back(probability.second); else  prob2D.push_back(-1.); 

          } 
    
         } // quality cuts if
     
      } //track loop
       TrackIPTagInfo tagInfo(ip2Dv,ip3Dv,dLenv,jetDistv,prob2D,prob3D,selectedTracks,
                              edm::Ref<JetTracksAssociationCollection>(jetTracksAssociation,i),
                              *pvRef);
       outCollection->push_back(tagInfo); 
     }
 
    std::auto_ptr<reco::TrackIPTagInfoCollection> result(outCollection);
   iEvent.put(result);
 
   if(!pvFound) delete pv; //dummy pv deleted
   delete pvRef;
}


#include "CondFormats/BTauObjects/interface/TrackProbabilityCalibration.h"
#include "RecoBTag/XMLCalibration/interface/CalibrationInterface.h"
#include "CondFormats/DataRecord/interface/BTagTrackProbability2DRcd.h"
#include "CondFormats/DataRecord/interface/BTagTrackProbability3DRcd.h"
#include "FWCore/Framework/interface/EventSetupRecord.h"
#include "FWCore/Framework/interface/EventSetupRecordImplementation.h"
#include "FWCore/Framework/interface/EventSetupRecordKey.h"


void TrackIPProducer::checkEventSetup(const EventSetup & iSetup)
 {
using namespace edm;
using namespace edm::eventsetup;
   const EventSetupRecord & re2D= iSetup.get<BTagTrackProbability2DRcd>();
   const EventSetupRecord & re3D= iSetup.get<BTagTrackProbability3DRcd>();
   unsigned long long cacheId2D= re2D.cacheIdentifier();
   unsigned long long cacheId3D= re3D.cacheIdentifier();

   if(cacheId2D!=m_calibrationCacheId2D || cacheId3D!=m_calibrationCacheId3D  )  //Calibration changed
   {
     //iSetup.get<BTagTrackProbabilityRcd>().get(calib);
     ESHandle<TrackProbabilityCalibration> calib2DHandle;
     iSetup.get<BTagTrackProbability2DRcd>().get(calib2DHandle);
     ESHandle<TrackProbabilityCalibration> calib3DHandle;
     iSetup.get<BTagTrackProbability3DRcd>().get(calib3DHandle);

     const TrackProbabilityCalibration *  ca2D= calib2DHandle.product();
     const TrackProbabilityCalibration *  ca3D= calib3DHandle.product();

     CalibrationInterface<TrackClassFilterCategory,CalibratedHistogramXML> * calib3d =  new CalibrationInterface<TrackClassFilterCategory,CalibratedHistogramXML>;
     CalibrationInterface<TrackClassFilterCategory,CalibratedHistogramXML> * calib2d =  new CalibrationInterface<TrackClassFilterCategory,CalibratedHistogramXML>;

     for(size_t i=0;i<ca3D->data.size(); i++)    
     {
        calib3d->addEntry(TrackClassFilterCategory(ca3D->data[i].category),ca3D->data[i].histogram); // convert category data to filtering category
     }
    
     for(size_t i=0;i<ca2D->data.size(); i++)    
     {
        calib2d->addEntry(TrackClassFilterCategory(ca2D->data[i].category),ca2D->data[i].histogram); // convert category data to filtering category
     }
  
     if(m_probabilityEstimator) delete m_probabilityEstimator;  //this should delete also old calib via estimator destructor
     m_probabilityEstimator=new HistogramProbabilityEstimator(calib3d,calib2d);

   }
   m_calibrationCacheId3D=cacheId3D;
   m_calibrationCacheId2D=cacheId2D;
}


//define this as a plug-in
DEFINE_FWK_MODULE(TrackIPProducer);

