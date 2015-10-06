#define __CL_ENABLE_EXCEPTIONS
#include <CL/cl.hpp>

#include <vector>
#include <fstream>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <assert.h>

const size_t BLOCK_SIZE = 16;

int main()
{
	std::vector<cl::Platform> platforms;
	std::vector<cl::Device> devices;
	std::vector<cl::Kernel> kernels;

	try {
		cl::Platform::get(&platforms);
		platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &devices);

		cl::Context context(devices);

		cl::CommandQueue queue(context, devices[0], CL_QUEUE_PROFILING_ENABLE);

		std::ifstream cl_file("convolution.cl");
		std::string cl_string(std::istreambuf_iterator<char>(cl_file), (std::istreambuf_iterator<char>()));
		cl::Program::Sources source(1, std::make_pair(cl_string.c_str(),
			cl_string.length() + 1));

		cl::Program program(context, source);
		program.build(devices);
		cl::Kernel kernel_hs(program, "convolution");
		auto convolution = cl::make_kernel<cl::Buffer&, cl::Buffer&, cl::Buffer&, int, int>(kernel_hs);

		std::ifstream input("input.txt");
		int n;
		int m;
		input >> n >> m;
		size_t n_squared = n * n;
		size_t m_squared = m * m;

		cl::Buffer dev_input_a(context, CL_MEM_READ_ONLY, sizeof(float) * n_squared);
		cl::Buffer dev_input_b(context, CL_MEM_READ_ONLY, sizeof(float) * m_squared);
		cl::Buffer dev_output(context, CL_MEM_WRITE_ONLY, sizeof(float) * n_squared);

		{
			std::vector<float> input_a(n_squared);
			for (float &matrix_elem : input_a)
			{
				input >> matrix_elem;
			}
			queue.enqueueWriteBuffer(dev_input_a, CL_TRUE, 0, sizeof(float) * n_squared, &input_a.front());
		}

		{
			std::vector<float> input_b(m_squared);
			for (float &matrix_elem : input_b)
			{
				input >> matrix_elem;
			}
			queue.enqueueWriteBuffer(dev_input_b, CL_TRUE, 0, sizeof(float) * m_squared, &input_b.front());
		}

		input.close();
		//float zero_float = 0;
		//cl_int err = queue.enqueueFillBuffer(dev_output, 0.0f, 0, sizeof(float) * n_squared);
		//assert(!err);
		queue.finish();
		size_t n_up = n % BLOCK_SIZE == 0 ? n : (n / BLOCK_SIZE + 1) * BLOCK_SIZE;
		cl::EnqueueArgs eargs(queue, cl::NullRange, cl::NDRange(n_up, n_up), cl::NDRange(BLOCK_SIZE, BLOCK_SIZE));

		cl::Event event = convolution(eargs, dev_input_a, dev_input_b, dev_output, n, m);
		event.wait();

		cl_ulong start_time = event.getProfilingInfo<CL_PROFILING_COMMAND_START>();
		cl_ulong end_time = event.getProfilingInfo<CL_PROFILING_COMMAND_END>();
		cl_ulong elapsed_time = end_time - start_time;

		{
			std::vector<float> output(n_squared);
			queue.enqueueReadBuffer(dev_output, CL_TRUE, 0, sizeof(float) * n_squared, &output.front());
			std::ofstream output_file("output.txt");
			for (size_t i = 0; i < n; ++i)
			{
				for (size_t j = 0; j < n; ++j)
				{
					if (j != 0)
					{
						output_file << ' ';
					}
					output_file << output[i * n + j];
				}
				output_file << std::endl;
			}
			output_file.close();
		}

		std::cout << std::setprecision(2) << "Total time: " << elapsed_time / 1000000.0 << " ms" << std::endl;
	}
	catch (cl::Error e)
	{
		std::cerr << "Exception building/running cl programm" << std::endl << e.what() << " : " << e.err() << std::endl;
		return -1;
	}
	catch (std::ifstream::failure e)
	{
		std::cerr << "Exception opening/reading/closing file" << std::endl << e.what() << " : " << e.code() << std::endl;
		return -2;
	}

	return 0;
}