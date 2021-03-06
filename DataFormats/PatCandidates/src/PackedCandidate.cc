#include "DataFormats/PatCandidates/interface/PackedCandidate.h"
#include "DataFormats/SiPixelDetId/interface/PixelSubdetector.h"
#include "DataFormats/SiStripDetId/interface/StripSubdetector.h"
#include "DataFormats/PatCandidates/interface/libminifloat.h"
#include "DataFormats/Math/interface/deltaPhi.h"

#include "DataFormats/PatCandidates/interface/liblogintpack.h"
using namespace logintpack;

CovarianceParameterization pat::PackedCandidate::covarianceParameterization_;
std::once_flag pat::PackedCandidate::covariance_load_flag;

void pat::PackedCandidate::pack(bool unpackAfterwards) {
    packedPt_  =  MiniFloatConverter::float32to16(p4_.load()->Pt());
    packedEta_ =  int16_t(std::round(p4_.load()->Eta()/6.0f*std::numeric_limits<int16_t>::max()));
    packedPhi_ =  int16_t(std::round(p4_.load()->Phi()/3.2f*std::numeric_limits<int16_t>::max()));
    packedM_   =  MiniFloatConverter::float32to16(p4_.load()->M());
    if (unpackAfterwards) {
      delete p4_.exchange(nullptr);
      delete p4c_.exchange(nullptr);
      unpack(); // force the values to match with the packed ones
    }
}

void pat::PackedCandidate::packVtx(bool unpackAfterwards) {
    reco::VertexRef pvRef = vertexRef();
    Point pv = pvRef.isNonnull() ? pvRef->position() : Point();
    float dxPV = vertex_.load()->X() - pv.X(), dyPV = vertex_.load()->Y() - pv.Y(); //, rPV = std::hypot(dxPV, dyPV);
    float s = std::sin(float(p4_.load()->Phi())+dphi_), c = std::cos(float(p4_.load()->Phi()+dphi_)); // not the fastest option, but we're in reduced precision already, so let's avoid more roundoffs
    dxy_  = - dxPV * s + dyPV * c;    
    // if we want to go back to the full x,y,z we need to store also
    // float dl = dxPV * c + dyPV * s; 
    // float xRec = - dxy_ * s + dl * c, yRec = dxy_ * c + dl * s;
    float pzpt = p4_.load()->Pz()/p4_.load()->Pt();
    dz_ = vertex_.load()->Z() - pv.Z() - (dxPV*c + dyPV*s) * pzpt;
    packedDxy_ = MiniFloatConverter::float32to16(dxy_*100);
    packedDz_   = pvRef.isNonnull() ? MiniFloatConverter::float32to16(dz_*100) : int16_t(std::round(dz_/40.f*std::numeric_limits<int16_t>::max()));
    packedDPhi_ =  int16_t(std::round(dphi_/3.2f*std::numeric_limits<int16_t>::max()));
    packedDEta_ =  MiniFloatConverter::float32to16(deta_);
    packedDTrkPt_ = MiniFloatConverter::float32to16(dtrkpt_);
 
    if (unpackAfterwards) {
      delete vertex_.exchange(nullptr);
      unpackVtx();
    }
}

void pat::PackedCandidate::unpack() const {
    float pt = MiniFloatConverter::float16to32(packedPt_);
    double shift = (pt<1. ? 0.1*pt : 0.1/pt); // shift particle phi to break degeneracies in angular separations
    double sign = ( ( int(pt*10) % 2 == 0 ) ? 1 : -1 ); // introduce a pseudo-random sign of the shift
    double phi = int16_t(packedPhi_)*3.2f/std::numeric_limits<int16_t>::max() + sign*shift*3.2/std::numeric_limits<int16_t>::max();
    auto p4 = std::make_unique<PolarLorentzVector>(pt,
                             int16_t(packedEta_)*6.0f/std::numeric_limits<int16_t>::max(),
                             phi,
                             MiniFloatConverter::float16to32(packedM_));
    auto p4c = std::make_unique<LorentzVector>( *p4 );
    PolarLorentzVector* expectp4= nullptr;
    if( p4_.compare_exchange_strong(expectp4,p4.get()) ) {
      p4.release();
    }

    //p4c_ works as the guard for unpacking so it
    // must be set last
    LorentzVector* expectp4c = nullptr;
    if(p4c_.compare_exchange_strong(expectp4c, p4c.get()) ) {
      p4c.release();
    }
}

void pat::PackedCandidate::packCovariance(const reco::TrackBase::CovarianceMatrix &m, bool unpackAfterwards){
    packedCovariance_.dptdpt = packCovarianceElement(m,0,0);
    packedCovariance_.detadeta = packCovarianceElement(m,1,1);
    packedCovariance_.dphidphi = packCovarianceElement(m,2,2); 
    packedCovariance_.dxydxy =packCovarianceElement(m,3,3);
    packedCovariance_.dzdz = packCovarianceElement(m,4,4);
    packedCovariance_.dxydz = packCovarianceElement(m,3,4);
    packedCovariance_.dlambdadz = packCovarianceElement(m,1,4);
    packedCovariance_.dphidxy = packCovarianceElement(m,2,3);
   //unpack afterwards
   if(unpackAfterwards) unpackCovariance();
}

void pat::PackedCandidate::unpackCovariance() const {
    const CovarianceParameterization & p=covarianceParameterization();
    if(p.isValid()) 
    {
      auto m = std::make_unique<reco::TrackBase::CovarianceMatrix>() ;
      for(int i=0;i<5;i++)
        for(int j=0;j<5;j++){
          (*m)(i,j)=0;
      }
      unpackCovarianceElement(*m,packedCovariance_.dptdpt,0,0);
      unpackCovarianceElement(*m,packedCovariance_.detadeta,1,1);
      unpackCovarianceElement(*m,packedCovariance_.dphidphi,2,2);
      unpackCovarianceElement(*m,packedCovariance_.dxydxy,3,3);
      unpackCovarianceElement(*m,packedCovariance_.dzdz,4,4);
      unpackCovarianceElement(*m,packedCovariance_.dxydz,3,4);
      unpackCovarianceElement(*m,packedCovariance_.dlambdadz,1,4);
      unpackCovarianceElement(*m,packedCovariance_.dphidxy,2,3);
      reco::TrackBase::CovarianceMatrix* expected = nullptr;
      if( m_.compare_exchange_strong(expected,m.get()) ) {
         m.release();
      }

    } else {
     throw edm::Exception(edm::errors::UnimplementedFeature)
     << "You do not have a valid track parameters file loaded. "
     << "Please check that the release version is compatible with your input data"
     <<"or avoid accessing track parameter uncertainties. ";
    }
}

void pat::PackedCandidate::unpackVtx() const {
    reco::VertexRef pvRef = vertexRef();
    dphi_ = int16_t(packedDPhi_)*3.2f/std::numeric_limits<int16_t>::max(),
    deta_ = MiniFloatConverter::float16to32(packedDEta_);
    dtrkpt_ = MiniFloatConverter::float16to32(packedDTrkPt_);
    dxy_ = MiniFloatConverter::float16to32(packedDxy_)/100.;
    dz_   = pvRef.isNonnull() ? MiniFloatConverter::float16to32(packedDz_)/100. : int16_t(packedDz_)*40.f/std::numeric_limits<int16_t>::max();
    Point pv = pvRef.isNonnull() ? pvRef->position() : Point();
    float phi = p4_.load()->Phi()+dphi_, s = std::sin(phi), c = std::cos(phi);
    auto vertex = std::make_unique<Point>(pv.X() - dxy_ * s,
                    pv.Y() + dxy_ * c,
                    pv.Z() + dz_ ); // for our choice of using the PCA to the PV, by definition the remaining term -(dx*cos(phi) + dy*sin(phi))*(pz/pt) is zero

     

 
    Point* expected = nullptr;
    if( vertex_.compare_exchange_strong(expected,vertex.get()) ) {
      vertex.release();
    }
}

pat::PackedCandidate::~PackedCandidate() { 
  delete p4_.load();
  delete p4c_.load();
  delete vertex_.load();
  delete track_.load();
  delete m_.load();
}


float pat::PackedCandidate::dxy(const Point &p) const {
	maybeUnpackBoth();
	const float phi = float(p4_.load()->Phi())+dphi_;
	return -(vertex_.load()->X()-p.X()) * std::sin(phi) + (vertex_.load()->Y()-p.Y()) * std::cos(phi);
}
float pat::PackedCandidate::dz(const Point &p) const {
    maybeUnpackBoth();
    const float phi = float(p4_.load()->Phi())+dphi_;
    return (vertex_.load()->Z()-p.Z())  - ((vertex_.load()->X()-p.X()) * std::cos(phi) + (vertex_.load()->Y()-p.Y()) * std::sin(phi)) * p4_.load()->Pz()/p4_.load()->Pt();
}

void pat::PackedCandidate::unpackTrk() const {
    maybeUnpackBoth();
    math::RhoEtaPhiVector p3(ptTrk(),etaAtVtx(),phiAtVtx());
    maybeUnpackCovariance();
    int numberOfStripLayers = stripLayersWithMeasurement(), numberOfPixelLayers = pixelLayersWithMeasurement();
    int numberOfPixelHits = this->numberOfPixelHits();
    int numberOfHits = this->numberOfHits();

    int ndof = numberOfHits+numberOfPixelHits-5;
    reco::HitPattern hp, hpExpIn;
    int i=0;
    LostInnerHits innerLost = lostInnerHits();
    
    auto track = std::make_unique<reco::Track>(normalizedChi2_*ndof,ndof,*vertex_,math::XYZVector(p3.x(),p3.y(),p3.z()),charge(),*(m_.load()),reco::TrackBase::undefAlgorithm,reco::TrackBase::loose);
    
    // add hits to match the number of laters and validHitInFirstPixelBarrelLayer
    if(innerLost == validHitInFirstPixelBarrelLayer){
        // first we add one hit on the first barrel layer
        track->appendTrackerHitPattern(PixelSubdetector::PixelBarrel, 1, 0, TrackingRecHit::valid); 
        // then to encode the number of layers, we add more hits on distinct layers (B2, B3, B4, F1, ...)
        for(i++; i<numberOfPixelLayers; i++) {
            if (i <= 3) { 
                track->appendTrackerHitPattern(PixelSubdetector::PixelBarrel, i+1, 0, TrackingRecHit::valid); 
            } else {    
                track->appendTrackerHitPattern(PixelSubdetector::PixelEndcap, i-3, 0, TrackingRecHit::valid); 
            }
        }
    } else {
        // to encode the information on the layers, we add one valid hits per layer but skipping PXB1
        for(;i<numberOfPixelLayers; i++) {
            if (i <= 2 ) { 
                track->appendTrackerHitPattern(PixelSubdetector::PixelBarrel, i+2, 0, TrackingRecHit::valid); 
            } else {    
                track->appendTrackerHitPattern(PixelSubdetector::PixelEndcap, i-3, 0, TrackingRecHit::valid); 
            }
        }
    }
    // add extra hits (overlaps, etc), all on the first layer with a hit - to avoid increasing the layer count
    for(;i<numberOfPixelHits; i++) { 
       track->appendTrackerHitPattern(PixelSubdetector::PixelBarrel, (innerLost == validHitInFirstPixelBarrelLayer ? 1 : 2), 0, TrackingRecHit::valid); 
    }
    // now start adding strip layers, putting one hit on each layer so that the hitPattern.stripLayersWithMeasurement works.
    // we don't know what the layers where, so we just start with TIB (4 layers), then TOB (6 layers), then TEC (9)
    // and then TID(3), so that we can get a number of valid strip layers up to 4+6+9+3
    for(int sl = 0; sl < numberOfStripLayers; ++sl, ++i) {
        if      (sl < 4)    track->appendTrackerHitPattern(StripSubdetector::TIB,   sl   +1, 1, TrackingRecHit::valid);
        else if (sl < 4+6)  track->appendTrackerHitPattern(StripSubdetector::TOB, (sl- 4)+1, 1, TrackingRecHit::valid);
        else if (sl < 10+9) track->appendTrackerHitPattern(StripSubdetector::TEC, (sl-10)+1, 1, TrackingRecHit::valid);
        else if (sl < 19+3) track->appendTrackerHitPattern(StripSubdetector::TID, (sl-13)+1, 1, TrackingRecHit::valid);
        else break; // wtf?
    }
    // finally we account for extra strip hits beyond the one-per-layer added above. we put them all on TIB1,
    // to avoid incrementing the number of layersWithMeasurement.
    for(;i<numberOfHits;i++) {
          track->appendTrackerHitPattern(StripSubdetector::TIB, 1, 1, TrackingRecHit::valid);
    }


    switch (innerLost) {
        case validHitInFirstPixelBarrelLayer:
            break;
        case noLostInnerHits:
            break;
        case oneLostInnerHit:
            track->appendTrackerHitPattern(PixelSubdetector::PixelBarrel, 1, 0, TrackingRecHit::missing_inner);
            break;
        case moreLostInnerHits:
            track->appendTrackerHitPattern(PixelSubdetector::PixelBarrel, 1, 0, TrackingRecHit::missing_inner);
            track->appendTrackerHitPattern(PixelSubdetector::PixelBarrel, 2, 0, TrackingRecHit::missing_inner);
            break;
    };

    if (trackHighPurity()) track->setQuality(reco::TrackBase::highPurity);
    
    reco::Track* expected = nullptr;
    if( track_.compare_exchange_strong(expected,track.get()) ) {
      track.release();
    }


}

//// Everything below is just trivial implementations of reco::Candidate methods

const reco::CandidateBaseRef & pat::PackedCandidate::masterClone() const {
  throw cms::Exception("Invalid Reference")
    << "this Candidate has no master clone reference."
    << "Can't call masterClone() method.\n";
}

bool pat::PackedCandidate::hasMasterClone() const {
  return false;
}

bool pat::PackedCandidate::hasMasterClonePtr() const {
  return false;
}


const reco::CandidatePtr & pat::PackedCandidate::masterClonePtr() const {
  throw cms::Exception("Invalid Reference")
    << "this Candidate has no master clone ptr."
    << "Can't call masterClonePtr() method.\n";
}

size_t pat::PackedCandidate::numberOfDaughters() const { 
  return 0; 
}

size_t pat::PackedCandidate::numberOfMothers() const { 
  return 0; 
}

bool pat::PackedCandidate::overlap( const reco::Candidate & o ) const { 
  return  p4() == o.p4() && vertex() == o.vertex() && charge() == o.charge();
//  return  p4() == o.p4() && charge() == o.charge();
}

const reco::Candidate * pat::PackedCandidate::daughter( size_type ) const {
  return 0;
}

const reco::Candidate * pat::PackedCandidate::mother( size_type ) const {
  return 0;
}

const reco::Candidate * pat::PackedCandidate::daughter(const std::string&) const {
  throw edm::Exception(edm::errors::UnimplementedFeature)
    << "This Candidate type does not implement daughter(std::string). "
    << "Please use CompositeCandidate or NamedCompositeCandidate.\n";
}

reco::Candidate * pat::PackedCandidate::daughter(const std::string&) {
  throw edm::Exception(edm::errors::UnimplementedFeature)
    << "This Candidate type does not implement daughter(std::string). "
    << "Please use CompositeCandidate or NamedCompositeCandidate.\n";
}



reco::Candidate * pat::PackedCandidate::daughter( size_type ) {
  return 0;
}

double pat::PackedCandidate::vertexChi2() const {
  return 0;
}

double pat::PackedCandidate::vertexNdof() const {
  return 0;
}

double pat::PackedCandidate::vertexNormalizedChi2() const {
  return 0;
}

double pat::PackedCandidate::vertexCovariance(int i, int j) const {
  throw edm::Exception(edm::errors::UnimplementedFeature)
    << "reco::ConcreteCandidate does not implement vertex covariant matrix.\n";
}

void pat::PackedCandidate::fillVertexCovariance(CovarianceMatrix & err) const {
  throw edm::Exception(edm::errors::UnimplementedFeature)
    << "reco::ConcreteCandidate does not implement vertex covariant matrix.\n";
}


bool pat::PackedCandidate::longLived() const {return false;}

bool pat::PackedCandidate::massConstraint() const {return false;}

// puppiweight
void pat::PackedCandidate::setPuppiWeight(float p, float p_nolep) {
  // Set both weights at once to avoid misconfigured weights if called in the wrong order
  packedPuppiweight_ = pack8logClosed((p-0.5)*2,-2,0,64);
  packedPuppiweightNoLepDiff_ = pack8logClosed((p_nolep-0.5)*2,-2,0,64) - packedPuppiweight_;
}

float pat::PackedCandidate::puppiWeight() const { return unpack8logClosed(packedPuppiweight_,-2,0,64)/2. + 0.5;}

float pat::PackedCandidate::puppiWeightNoLep() const { return unpack8logClosed(packedPuppiweightNoLepDiff_+packedPuppiweight_,-2,0,64)/2. + 0.5;}

void pat::PackedCandidate::setRawCaloFraction(float p) {
  if(100*p>std::numeric_limits<uint8_t>::max())
    rawCaloFraction_ = std::numeric_limits<uint8_t>::max(); // Set to overflow value
  else
    rawCaloFraction_ = 100*p;
}

void pat::PackedCandidate::setHcalFraction(float p) {
  hcalFraction_ = 100*p;
}

void pat::PackedCandidate::setIsIsolatedChargedHadron(bool p) {
  isIsolatedChargedHadron_ = p;
}
