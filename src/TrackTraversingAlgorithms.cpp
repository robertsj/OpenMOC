#include "TrackTraversingAlgorithms.h"
#include "CPUSolver.h"


//TODO: description
MaxOpticalLength::MaxOpticalLength(TrackGenerator* track_generator)
                                 : TraverseSegments(track_generator) {
  _max_tau = 0;
}


//TODO: description
void MaxOpticalLength::execute() {
  FP_PRECISION infinity = std::numeric_limits<FP_PRECISION>::max();
  _track_generator->setMaxOpticalLength(infinity);
#pragma omp parallel
  {
    MOCKernel** kernels = getKernels<SegmentationKernel>();
    loopOverTracks(kernels);
  }
  _track_generator->setMaxOpticalLength(_max_tau);
}


//TODO: description
void MaxOpticalLength::onTrack(Track* track, segment* segments) {
  for (int s=0; s < track->getNumSegments(); s++) {
    FP_PRECISION length = segments[s]._length;
    Material* material = segments[s]._material;
    FP_PRECISION* sigma_t = material->getSigmaT();

    for (int e=0; e < material->getNumEnergyGroups(); e++) {
      FP_PRECISION tau = length*sigma_t[e];
      if (tau > _max_tau) {
#pragma omp critical
        _max_tau = std::max(_max_tau, tau);
      }
    }
  }
}


/*
  TODO: class description
*/

//TODO: description
SegmentCounter::SegmentCounter(TrackGenerator* track_generator)
                               : TraverseSegments(track_generator) {
  _max_num_segments = 0;
}


//TODO: description
void SegmentCounter::execute() {
#pragma omp parallel
  {
    MOCKernel** kernels = getKernels<CounterKernel>();
    loopOverTracks(kernels);
  }
  _track_generator->setMaxNumSegments(_max_num_segments);
}


//TODO: description
void SegmentCounter::onTrack(Track* track, segment* segments) {
  if (track->getNumSegments() > _max_num_segments) {
#pragma omp critical
    _max_num_segments = std::max(_max_num_segments, track->getNumSegments());
  }
}


/*
  TODO: class description
*/

//TODO: description
SegmentSplitter::SegmentSplitter(TrackGenerator* track_generator)
                               : TraverseSegments(track_generator) {
}


//TODO: description
void SegmentSplitter::execute() {
#pragma omp parallel
  {
    MOCKernel** kernels = getKernels<SegmentationKernel>();
    loopOverTracks(kernels);
  }
}


//TODO: description
void SegmentSplitter::onTrack(Track* track, segment* segments) {

  /* Get the max optical length from the TrackGenerator */
  FP_PRECISION max_optical_length =
    _track_generator->retrieveMaxOpticalLength();

  /* Extract data from this segment to compute its optical
   * length */
  for (int s = 0; s < track->getNumSegments(); s++) {
    segment* curr_segment = track->getSegment(s);
    Material* material = curr_segment->_material;
    FP_PRECISION length = curr_segment->_length;
    int fsr_id = curr_segment->_region_id;

    /* Compute number of segments to split this segment into */
    int min_num_cuts = 1;
    int num_groups = material->getNumEnergyGroups();
    FP_PRECISION* sigma_t = material->getSigmaT();

    for (int g=0; g < num_groups; g++) {
      FP_PRECISION tau = length * sigma_t[g];
      int num_cuts = ceil(tau / max_optical_length);
      min_num_cuts = std::max(num_cuts, min_num_cuts);
    }

    /* If the segment does not need subdivisions, go to next
     * segment */
    if (min_num_cuts == 1)
      continue;

    /* Record the CMFD surfaces */
    int cmfd_surface_fwd = curr_segment->_cmfd_surface_fwd;
    int cmfd_surface_bwd = curr_segment->_cmfd_surface_bwd;

    /* Split the segment into sub-segments */
    for (int k=0; k < min_num_cuts; k++) {

      /* Create a new Track segment */
      segment* new_segment = new segment;
      new_segment->_material = material;
      new_segment->_length = length / FP_PRECISION(min_num_cuts);
      new_segment->_region_id = fsr_id;

      /* Assign CMFD surface boundaries */
      if (k == 0)
        new_segment->_cmfd_surface_bwd = cmfd_surface_bwd;

      if (k == min_num_cuts-1)
        new_segment->_cmfd_surface_fwd = cmfd_surface_fwd;

      /* Insert the new segment to the Track */
      track->insertSegment(s+k+1, new_segment);
    }

    /* Remove the original segment from the Track */
    track->removeSegment(s);
  }
}


/*
   TODO: class description
*/

//TODO: description
VolumeCalculator::VolumeCalculator(TrackGenerator* track_generator)
                                  : TraverseSegments(track_generator) {
}


//TODO: description
void VolumeCalculator::execute() {
#pragma omp parallel
  {
    MOCKernel** kernels = getKernels<VolumeKernel>();
    loopOverTracks(kernels);
  }
}


//TODO: description
void VolumeCalculator::onTrack(Track* track, segment* segments) {
}


/*
   TODO: class description
*/

//TODO: description
CentroidGenerator::CentroidGenerator(TrackGenerator* track_generator)
                                  : TraverseSegments(track_generator) {

  _FSR_volumes = track_generator->getFSRVolumesBuffer();
  _FSR_locks = track_generator->getFSRLocks();
}


//TODO: description
void CentroidGenerator::execute() {
#pragma omp parallel
  {
    MOCKernel** kernels = getKernels<SegmentationKernel>();
    loopOverTracks(kernels);
  }
}


//TODO: description
void CentroidGenerator::setCentroids(Point** centroids) {
  _centroids = centroids;
}


//TODO: description
void CentroidGenerator::onTrack(Track* track, segment* segments) {

  /* Extract track information */
  Point* start = track->getStart();
  double xx = start->getX();
  double yy = start->getY();
  double zz = start->getZ();
  double wgt = track->getWeight();
  double phi = track->getPhi();

  /* Get polar angles depending on the dimensionality */
  double sin_theta = 1;
  double cos_theta = 0;
  Track3D* track_3D = dynamic_cast<Track3D*>(track);
  if (track_3D != NULL) {
    double theta = track_3D->getTheta();
    sin_theta = sin(theta);
    cos_theta = cos(theta);
  }

  /* Loop over segments to accumlate contribution to centroids */
  for (int s=0; s < track->getNumSegments(); s++) {
    segment* curr_segment = &segments[s];
    int fsr = curr_segment->_region_id;
    double volume = _FSR_volumes[fsr];

    /* Set the lock for this FSR */
    omp_set_lock(&_FSR_locks[fsr]);

    _centroids[fsr]->
        setX(_centroids[fsr]->getX() + wgt *
        (xx + cos(phi) * sin_theta * curr_segment->_length / 2.0)
        * curr_segment->_length / _FSR_volumes[fsr]);

    _centroids[fsr]->
        setY(_centroids[fsr]->getY() + wgt *
        (yy + sin(phi) * sin_theta * curr_segment->_length / 2.0)
        * curr_segment->_length / _FSR_volumes[fsr]);

    _centroids[fsr]->
        setZ(_centroids[fsr]->getZ() + wgt *
        (zz + cos_theta * curr_segment->_length / 2.0)
        * curr_segment->_length / _FSR_volumes[fsr]);

    /* Unset the lock for this FSR */
    omp_unset_lock(&_FSR_locks[fsr]);

    xx += cos(phi) * sin_theta * curr_segment->_length;
    yy += sin(phi) * cos_theta * curr_segment->_length;
    zz += cos_theta * curr_segment->_length;
  }
}


/*
   TODO: class description
*/


//TODO: description
TransportSweep::TransportSweep(TrackGenerator* track_generator)
                              : TraverseSegments(track_generator) {
  _cpu_solver = NULL;
}


//TODO: description
void TransportSweep::execute() {
#pragma omp parallel
  {
    MOCKernel** kernels = getKernels<SegmentationKernel>();
    loopOverTracks(kernels);
  }
}


//TODO: description
void TransportSweep::setCPUSolver(CPUSolver* cpu_solver) {
  _cpu_solver = cpu_solver;
}


//TODO: description
void TransportSweep::onTrack(Track* track, segment* segments) {

  /* Allocate temporary FSR flux locally */
  int num_groups = _track_generator->getGeometry()->getNumEnergyGroups();
  FP_PRECISION thread_fsr_flux[num_groups];

  /* Extract Track information */
  int track_id = track->getUid();
  int azim_index = track->getAzimIndex();
  int num_segments = track->getNumSegments();
  FP_PRECISION* track_flux;

  /* Extract the polar index if a 3D track */
  int polar_index = 0;
  Track3D* track_3D = dynamic_cast<Track3D*>(track);
  if (track_3D != NULL)
    polar_index = track_3D->getPolarIndex();

  /* Correct azimuthal index to first octant */
  Quadrature* quad = _track_generator->getQuadrature();
  azim_index = quad->getFirstOctantAzim(azim_index);

  /* Get the forward track flux */
  track_flux = _cpu_solver->getBoundaryFlux(track_id, true);

  /* Loop over each Track segment in forward direction */
  for (int s=0; s < num_segments; s++) {
    segment* curr_segment = &segments[s];
    _cpu_solver->tallyScalarFlux(curr_segment, azim_index, polar_index,
                                 track_flux, thread_fsr_flux);
    _cpu_solver->tallySurfaceCurrent(curr_segment, azim_index, polar_index,
                                     track_flux, true);
  }

  /* Transfer boundary angular flux to outgoing Track */
  _cpu_solver->transferBoundaryFlux(track_id, azim_index, polar_index, true,
                                    track_flux);

  /* Get the backward track flux */
  track_flux = _cpu_solver->getBoundaryFlux(track_id, false);

  /* Loop over each Track segment in reverse direction */
  for (int s=num_segments-1; s >= 0; s--) {
    segment* curr_segment = &segments[s];
    _cpu_solver->tallyScalarFlux(curr_segment, azim_index, polar_index,
                                 track_flux, thread_fsr_flux);
    _cpu_solver->tallySurfaceCurrent(curr_segment, azim_index, polar_index,
                                     track_flux, false);
  }

  /* Transfer boundary angular flux to outgoing Track */
  _cpu_solver->transferBoundaryFlux(track_id, azim_index, polar_index, false,
                                    track_flux);
}


/*
   TODO: class description
*/

//TODO: description
DumpSegments::DumpSegments(TrackGenerator* track_generator)
                           : TraverseSegments(track_generator) {
  _out = NULL;
}


//TODO: description
void DumpSegments::execute() {
  MOCKernel** kernels = getKernels<SegmentationKernel>();
  loopOverTracks(kernels);
}


//TODO: description
void DumpSegments::setOutputFile(FILE* out) {
  _out = out;
}


//TODO: description
void DumpSegments::onTrack(Track* track, segment* segments) {

  /* Write data for this Track to the Track file */
  int num_segments = track->getNumSegments();
  fwrite(&num_segments, sizeof(int), 1, _out);

  /* Get CMFD mesh object */
  Cmfd* cmfd = _track_generator->getGeometry()->getCmfd();

  /* Loop over all segments for this Track */
  for (int s=0; s < num_segments; s++) {

    /* Get data for this segment */
    segment* curr_segment = track->getSegment(s);
    FP_PRECISION length = curr_segment->_length;
    int material_id = curr_segment->_material->getId();
    int region_id = curr_segment->_region_id;

    /* Write data for this segment to the Track file */
    fwrite(&length, sizeof(double), 1, _out);
    fwrite(&material_id, sizeof(int), 1, _out);
    fwrite(&region_id, sizeof(int), 1, _out);

    /* Write CMFD-related data for the Track if needed */
    if (cmfd != NULL) {
      int cmfd_surface_fwd = curr_segment->_cmfd_surface_fwd;
      int cmfd_surface_bwd = curr_segment->_cmfd_surface_bwd;
      fwrite(&cmfd_surface_fwd, sizeof(int), 1, _out);
      fwrite(&cmfd_surface_bwd, sizeof(int), 1, _out);
    }
  }
}


/*
   TODO: class description
*/

//TODO: description
ReadSegments::ReadSegments(TrackGenerator* track_generator)
                           : TraverseSegments(track_generator) {
  _in = NULL;
}


//TODO: description
void ReadSegments::execute() {
  MOCKernel** kernels = getKernels<SegmentationKernel>();
  loopOverTracks(kernels);
}


//TODO: description
void ReadSegments::setInputFile(FILE* in) {
  _in = in;
}


//TODO: description
void ReadSegments::onTrack(Track* track, segment* segments) {

  /* Get CMFD mesh object */
  Geometry* geometry = _track_generator->getGeometry();
  Cmfd* cmfd = geometry->getCmfd();
  int ret;

  /* Get materials map */
  std::map<int, Material*> materials = geometry->getAllMaterials();

  /* Import data for this Track from Track file */
  int num_segments;
  ret = fread(&num_segments, sizeof(int), 1, _in);

  /* Loop over all segments in this Track */
  for (int s=0; s < num_segments; s++) {

    /* Import data for this segment from Track file */
    double length;
    ret = fread(&length, sizeof(double), 1, _in);
    int material_id;
    ret = fread(&material_id, sizeof(int), 1, _in);
    int region_id;
    ret = fread(&region_id, sizeof(int), 1, _in);

    /* Initialize segment with the data */
    segment* curr_segment = new segment;
    curr_segment->_length = length;
    curr_segment->_material = materials[material_id];
    curr_segment->_region_id = region_id;

    /* Import CMFD-related data if needed */
    if (cmfd != NULL) {
      int cmfd_surface_fwd;
      ret = fread(&cmfd_surface_fwd, sizeof(int), 1, _in);
      curr_segment->_cmfd_surface_fwd = cmfd_surface_fwd;
      int cmfd_surface_bwd;
      ret = fread(&cmfd_surface_bwd, sizeof(int), 1, _in);
      curr_segment->_cmfd_surface_bwd = cmfd_surface_bwd;
    }

    /* Add this segment to the Track */
    track->addSegment(curr_segment);
  }
}


