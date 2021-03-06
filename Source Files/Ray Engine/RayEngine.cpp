#include "RayEngine.h"
#include "CUDA/RayEngine.cuh"
#include "Utility/Timer.h"
#include <deque>
#include "Algorithms/Filters/Filter.h"
#include "GPGPU/GPUManager.h"

/* List of jobs */
static GPUBuffer<RayJob> jobList;

/* The render derivatives */
static std::deque<double> renderDerivatives;

/* The old render time */
static double oldRenderTime;

/* The frame hold */
static uint frameHold = 5;

/* timer. */
static Timer renderTimer;

static uint raysAllocated = 0;

static GPUBuffer<Ray> deviceRaysA;
static GPUBuffer<Ray> deviceRaysB;

//Scene* scene = nullptr;
static GPUBuffer<curandState> randomState;

static uint raySeedGl = 0;

static const uint rayDepth = 4;

static GPUBuffer<Scene> scene;

//stored counters
static GPUBuffer<int> counter;
static GPUBuffer<int> hitAtomic;

static GPUExecutePolicy persistantPolicy;

__host__ __device__ __inline__ uint WangHash(uint a) {
	a = (a ^ 61) ^ (a >> 16);
	a = a + (a << 3);
	a = a ^ (a >> 4);
	a = a * 0x27d4eb2d;
	a = a ^ (a >> 15);
	return a;
}

/*
 *    Updates the jobs.
 *    @param 		 	renderTime	The render time.
 *    @param 		 	targetTime	The target time.
 *    @param [in,out]	jobs	  	[in,out] If non-null, the jobs.
 */

void UpdateJobs(double renderTime, double targetTime, GPUBuffer<RayJob>& jobs) {

	//if it the first frame, pass the target as the '0th' frame
	if (renderDerivatives.size() == 0) {
		oldRenderTime = renderTime;
	}

	//count the jobs that can be modified
	int count = 0;
	for (auto& job : jobs) {
		if (job.canChange) {
			count++;
		}
	}

	//disperse equal cuts to all of them
	double countChange = 1.0f / count;

	//push the new derivative
	renderDerivatives.push_front(oldRenderTime - renderTime);
	oldRenderTime = renderTime;

	//cull the frame counts
	if (renderDerivatives.size() > frameHold) {
		renderDerivatives.pop_back();
	}

	//calculate the average derivative
	double averageRenderDV = 0.0;

	for (auto itrR = renderDerivatives.begin(); itrR != renderDerivatives.end(); ++itrR) {
		averageRenderDV += *itrR;
	}

	averageRenderDV /= renderDerivatives.size();

	//use the average derivative to grab an expected next frametime
	double expectedRender = renderTime + averageRenderDV;

	//target time -5% to account for frame instabilities and try to stay above target
	//double change = (targetTime / expectedRender - 1.0) * 0.95f * countChange + 1.0;

	double change = (targetTime - renderTime) / targetTime;



	//modify all the sample counts/ resolutions to reflect the change
	for (int i = 0; i < jobList.size(); i++) {

		RayJob& job = jobList[i];
		if (false) {

			Camera& camera = job.camera;

			float delta = change*camera.film.resolutionRatio;
			float newRatio = camera.film.resolutionRatio + delta;

			if (camera.film.resolutionMax.x * newRatio < 64) {

				newRatio = 64 / (float)camera.film.resolutionMax.x;

			}

			if (newRatio >= 1.0f) {

				camera.film.resolution = camera.film.resolutionMax;
				job.samples = newRatio;

			}
			else {

				camera.film.resolution.x = camera.film.resolutionMax.x * newRatio;
				camera.film.resolution.y = camera.film.resolutionMax.y * newRatio;
				job.samples = 1.0f;

			}

			//update the camera ratio
			camera.film.resolutionRatio = newRatio;

			//float tempSamples = job->samples * change;

			//GPUBuffer<Camera>* cameraBuffer = CameraManager::GetCameraBuffer();

			//Camera& camera = (*cameraBuffer)[job->camera];

			//if (tempSamples < 1.0f) {

			//	job->samples = 1.0f;
			//	float value = (1.0f - (job->samples - 1.0f) / (job->samples - tempSamples)) * change;
			//	camera.film.resolution.x = camera.film.resolutionMax.x * value;
			//	camera.film.resolution.y = camera.film.resolutionMax.y * value;
			//}
			//else {
			//	if (camera.film.resolution != camera.film.resolutionMax) {
			//		//job->camera.film.resolution *= change*countChange + 1.0;
			//	}
			//	job->samples = tempSamples;
			//}

		}
	}
}

/*
 *    Process this object.
 *    @param	scene 	The scene.
 *    @param	target	Target for the.
 */

void RayEngine::Process(const Scene* sceneIn, double target) {

	//start the timer once actual data movement and calculation starts
	renderTimer.Reset();

	const auto numberJobs = jobList.size();

	//only upload data if a job exists
	if (numberJobs > 0) {

		uint numberResults = 0;
		uint numberRays = 0;

		for (uint i = 0; i < numberJobs; ++i) {

			const Camera camera = jobList[i].camera;

			const uint rayAmount = camera.film.resolution.x*camera.film.resolution.y;

			jobList[i].rayOffset = numberResults;
			numberResults += rayAmount;

			if (jobList[i].samples < 0) {
				jobList[i].samples = 0.0f;
			}

			numberRays += rayAmount* uint(glm::ceil(jobList[i].samples));

		}

		if (numberResults != 0 && numberRays != 0) {

			jobList.TransferToDevice();

			//clear the jobs result memory, required for accumulation of multiple samples

			GPUDevice device = GPUManager::GetBestGPU();

			uint blockSize = 64;
			GPUExecutePolicy normalPolicy(glm::vec3((numberResults + blockSize - 1) / blockSize,1,1), glm::vec3(blockSize,1,1), 0, 0);

			device.Launch(normalPolicy, EngineSetup, numberResults, jobList, numberJobs);

			if (numberRays > raysAllocated) {

				if (deviceRaysA) {
					CudaCheck(cudaFree(deviceRaysA));
				}
				if (deviceRaysB) {
					CudaCheck(cudaFree(deviceRaysB));
				}
				if (randomState) {
					CudaCheck(cudaFree(randomState));
				}

				CudaCheck(cudaMalloc((void**)&randomState, numberRays * sizeof(curandState)));
				CudaCheck(cudaMalloc((void**)&deviceRaysA, numberRays * sizeof(Ray)));
				CudaCheck(cudaMalloc((void**)&deviceRaysB, numberRays * sizeof(Ray)));

				device.Launch(normalPolicy, RandomSetup, numberRays, randomState, WangHash(++raySeedGl));
				CudaCheck(cudaPeekAtLastError());
				CudaCheck(cudaDeviceSynchronize());

				raysAllocated = numberRays;

			}

			//copy the scene over
			CudaCheck(cudaMemcpy(scene, sceneIn, sizeof(Scene), cudaMemcpyHostToDevice));

			//setup the counters
			int zeroHost = 0;
			CudaCheck(cudaMemcpy(counter, &zeroHost, sizeof(int), cudaMemcpyHostToDevice));
			CudaCheck(cudaMemcpy(hitAtomic, &zeroHost, sizeof(int), cudaMemcpyHostToDevice));

			device.Launch(normalPolicy, RaySetup, numberRays, numberJobs, jobList, deviceRaysA, hitAtomic, randomState);
			CudaCheck(cudaPeekAtLastError());
			CudaCheck(cudaDeviceSynchronize());

			/*	Ray* hostRays = new Ray[numberRays];
			CudaCheck(cudaMemcpy(hostRays, deviceRaysA, numberRays *sizeof(Ray), cudaMemcpyDeviceToHost));

			for (int i = 0; i < numberRays; ++i) {

			}

			delete hostRays;*/

			//start the engine loop
			uint numActive;
			CudaCheck(cudaMemcpy(&numActive, hitAtomic, sizeof(int), cudaMemcpyDeviceToHost));

			for (uint i = 0; i < rayDepth && numActive>0; ++i) {

				//reset counters
				CudaCheck(cudaMemcpy(hitAtomic, &zeroHost, sizeof(int), cudaMemcpyHostToDevice));
				CudaCheck(cudaMemcpy(counter, &zeroHost, sizeof(int), cudaMemcpyHostToDevice));

				//grab the current block sizes for collecting hits based on numActive
				GPUExecutePolicy activePolicy(glm::vec3((numActive + blockSize - 1) / blockSize, 1, 1), glm::vec3(blockSize, 1, 1), 0, 0);

				//main engine, collects hits
				device.Launch(persistantPolicy, ExecuteJobs, numActive, deviceRaysA, scene, counter);
				CudaCheck(cudaPeekAtLastError());
				CudaCheck(cudaDeviceSynchronize());

				//processes hits 
				device.Launch(activePolicy, ProcessHits, numActive, jobList, numberJobs, deviceRaysA, deviceRaysB, scene, hitAtomic, randomState);
				CudaCheck(cudaPeekAtLastError());
				CudaCheck(cudaDeviceSynchronize());

				std::swap(deviceRaysA, deviceRaysB);

				CudaCheck(cudaMemcpy(&numActive, hitAtomic, sizeof(int), cudaMemcpyDeviceToHost));

			}
		}

	}

	UpdateJobs(renderTimer.Elapsed() / 1000.0, target, jobList);
}

/*
 *    Adds a job.
 *    @param 		 	whatToGet	The what to get.
 *    @param 		 	rayAmount	The ray amount.
 *    @param 		 	canChange	True if this object can change.
 *    @param 		 	samples  	The samples.
 *    @param [in,out]	camera   	The camera.
 *    @param [in,out]	resultsIn	If non-null, the results in.
 *    @param [in,out]	extraData	If non-null, information describing the extra.
 *    @return	Null if it fails, else a pointer to a RayJob.
 */

uint RayEngine::AddJob(rayType whatToGet, bool canChange,
	float samples) {

	jobList.push_back(RayJob(whatToGet, canChange, samples));

	return jobList[jobList.size() - 1].id;
}

/*
 *    Modify job.
 *    @param [in,out]	jobIn 	If non-null, the job in.
 *    @param [in,out]	camera	The camera.
 */

RayJob& RayEngine::GetJob(uint jobIn) {

	for (int i = 0; i < jobList.size(); i++) {

		RayJob& job = jobList[i];
		if (job.id == jobIn) {
			return job;
		}
	}

}

/*
 *    Removes the job described by job.
 *    @param [in,out]	job	If non-null, the job.
 *    @return	True if it succeeds, false if it fails.
 */

bool RayEngine::RemoveJob(uint job) {
	//TODO implement
	//jobList.remove(job);

	return true;
}

/* Initializes this object. */
void RayEngine::Initialize() {

	deviceRaysA.TransferDevice(GPUManager::GetBestGPU());
	deviceRaysB.TransferDevice(GPUManager::GetBestGPU());

	randomState.TransferDevice(GPUManager::GetBestGPU());

	counter.TransferDevice(GPUManager::GetBestGPU());
	hitAtomic.TransferDevice(GPUManager::GetBestGPU());

	scene.TransferDevice(GPUManager::GetBestGPU());

	scene.resize(1);
	counter.resize(1);
	hitAtomic.resize(1);
	
	persistantPolicy = GPUManager::GetBestGPU().BestExecutePolicy(ExecuteJobs);


	///////////////Alternative Hardcoded Calculation/////////////////
	//uint blockPerSM = CUDABackend::GetBlocksPerMP();
	//warpPerBlock = CUDABackend::GetWarpsPerMP() / blockPerSM;
	//blockCountE = CUDABackend::GetSMCount()*blockPerSM;
	//blockSizeE = dim3(CUDABackend::GetWarpSize(), warpPerBlock, 1);

	jobList.TransferDevice(GPUManager::GetBestGPU());

}

void RayEngine::Update() {
	for (int i = 0; i < jobList.size(); i++) {

		RayJob& job = jobList[i];
		job.camera.UpdateVariables();
	}
}

/* Terminates this object. */
void RayEngine::Terminate() {

}

void RayEngine::PreProcess() {

}
void RayEngine::PostProcess() {

	//grab job pointer
	auto job = *jobList.begin();

	//grab camera
	Camera camera = job.camera;

	//Filter::IterativeBicubic((glm::vec4*)job.camera.film.results, camera.film.resolution, camera.film.resolutionMax);
}
