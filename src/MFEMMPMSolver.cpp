
/**----------------------------------------------------------------------------
 Copyright (c) 2018-, UT-Battelle, LLC
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 * Neither the name of the copyright holder nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 Author(s): Jay Jay Billings (billingsjj <at> ornl <dot> gov)
 -----------------------------------------------------------------------------*/
#include <MFEMMPMSolver.h>
#include <set>
#include <MassMatrix.h>
#include <BasicMFEMGridMapper.h>
#include <ConstitutiveRelationshipService.h>
#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <iomanip>
#include <StringCaster.h>

using namespace std;
using namespace mfem;

namespace Kelvin {

MFEMMPMSolver::MFEMMPMSolver() {
	// TODO Auto-generated constructor stub

}

MFEMMPMSolver::~MFEMMPMSolver() {
	// TODO Auto-generated destructor stub
}

static void writeParticlePositions(MFEMMPMData & data,
		double ts) {

	string outputFSName = "kelvin_output_";
	outputFSName += to_string(ts);
	outputFSName += ".csv";
	ofstream outputFS(outputFSName);
	int dim = data.grid().dimension();
	outputFS << fixed;
	outputFS.precision(15);

	auto & particles = data.particles();
	int numParticles = particles.size();
	for (int i = 0; i < numParticles; i++) {
		auto & mPoint = particles[i];
		for (int j = 0; j < dim; j++) {
			outputFS << mPoint.pos[j] << ", ";
		}
		outputFS << ts << endl;
	}

	outputFS.close();
}


void MFEMMPMSolver::solve(MFEMMPMData & data) {

	// Get the properties
	auto & propertiesParser = data.properties();
	auto & properties = propertiesParser.getPropertyBlock("solver");

	// Assemble the grid
	auto & particles = data.particles();
	auto & grid = data.grid();
	grid.assemble(particles);
	// Setup a mapper for mapping from the grid to the material points
	BasicMFEMGridMapper mapper(data.meshContainer().getMesh());
	// Create a storage vector for velocities from the mapper
	int dim = data.meshContainer().dimension();
	int numParticles = particles.size();
	std::vector<double> velUpdate(numParticles*dim);

	// Set basic start time parameters - FIXME! Will read from input
	double tInit = fire::StringCaster<double>::cast(
			properties.at("startTime"));
	double tFinal = fire::StringCaster<double>::cast(
			properties.at("finalTime"));
	double dt = fire::StringCaster<double>::cast(
			properties.at("initialTimeStep"));
	double t = tInit; // dtOverstep = tFinal % dt;
	int numTimeSteps = (int) (tFinal/dt) + 1;
	int printStepFrequency =  fire::StringCaster<int>::cast(
			properties.at("outputStepFrequency"));

	// Set the body forces on the particles
	for (int i = 0; i < numParticles; i++) {
		particles[i].bodyForce[dim-1] = -9.8;
	}

	// FIXME! time stepping issues
	// 1) Handle final time overstepping
	// 2) Follow time step limitations described in the mpm-libre article

	// Integrate over time. At the moment this will not integrate correctly to
	// tFinal. See time stepping issues above - just a placeholder for now.
	for (int ts = 0; ts < numTimeSteps + 1; ts++) {
		t += dt;
		// Compute the acceleration at the grid nodes
		grid.updateNodalAccelerations(dt, particles);
		// Compute the initial velocity from the momenta
		grid.updateNodalVelocitiesFromMomenta(particles);
		// Apply boundary conditions
		grid.applyNoSlipBoundaryConditions();
		// Compute the velocity update
		grid.updateNodalVelocities(dt, particles);

		// Use mapping functions to compute the velocity and acceleration at
		// the material points
		mapper.updateParticleAccelerations(grid, particles);
		mapper.updateParticleVelocities(grid, particles, velUpdate);

		// Compute updates to the material point stresses and strains using the
		// appropriate constitutive relationship. Update the positions and
		// velocity using explicit integration. This is just a simple
		// explicit Euler update.
		for (int i = 0; i < numParticles; i++) {
			auto & mPoint = particles[i];
			// Get the constitutive equations
			auto & conRel = ConstitutiveRelationshipService::get(
					mPoint.materialId);
			// Compute/update the stress at material points using the
			// constitutive equation
			conRel.updateStrainRate(grid, mPoint);
			conRel.updateStress(grid, mPoint);
			// Update the positions and velocities
			for (int j = 0; j < dim; j++) {
				mPoint.pos[j] += dt * velUpdate[i * dim + j];
				mPoint.vel[j] += dt * mPoint.acc[j];
			}
		}

		// Print stepping information
		if (!(ts % printStepFrequency)) {
			cout << "dt = " << dt << ", ts = " << ts << ", t = "
					<< (tInit + dt * ts) << endl;
			writeParticlePositions(data, ts);
		}

	}

	return;
}


} /* namespace Kelvin */
