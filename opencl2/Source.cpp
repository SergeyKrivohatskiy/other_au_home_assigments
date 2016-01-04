#define __CL_ENABLE_EXCEPTIONS
#include <CL/cl.hpp>

#include <vector>
#include <fstream>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <assert.h>

const size_t BLOCK_SIZE = 256; // 2^x <= 256

std::vector<float> scan(size_t n, cl::Buffer &dev_input, cl::Buffer &dev_output, 
	cl::make_kernel<cl::Buffer&, cl::Buffer&, int> &block_sums_kernel, 
	cl::make_kernel<cl::Buffer&, cl::Buffer&, cl::Buffer&, int> &block_add_kernel,
	cl::CommandQueue &queue, cl::Context &context)
{
	size_t n_up = n % BLOCK_SIZE == 0 ? n : (n / BLOCK_SIZE + 1) * BLOCK_SIZE;
	cl::EnqueueArgs eargs(queue, cl::NullRange, cl::NDRange(n_up), cl::NDRange(BLOCK_SIZE));

	block_sums_kernel(eargs, dev_input, dev_output, (int)n).wait();

	std::vector<float> output(n);
	queue.enqueueReadBuffer(dev_output, CL_TRUE, 0, sizeof(float) * output.size(), &output.front());

	if (n <= BLOCK_SIZE)
	{
		return output;
	}

	std::vector<float> block_sums;
	block_sums.reserve(n / BLOCK_SIZE);
	for (size_t add_idx = BLOCK_SIZE - 1; add_idx < n; add_idx += BLOCK_SIZE)
	{
		block_sums.push_back(output[add_idx]);
	}
	assert(block_sums.size() == n / BLOCK_SIZE);

	cl::Buffer subtask_input(context, CL_MEM_READ_WRITE, sizeof(float) * block_sums.size());
	queue.enqueueWriteBuffer(
		subtask_input, CL_TRUE, 0, sizeof(float) * block_sums.size(), &block_sums.front());
	queue.finish();

	std::vector<float> block_additions = 
		scan(block_sums.size(), subtask_input, dev_input, 
			block_sums_kernel, block_add_kernel, queue, context);

	queue.enqueueWriteBuffer(
		subtask_input, CL_TRUE, 0, sizeof(float) * block_additions.size(), &block_additions.front());
	queue.finish();

	block_add_kernel(eargs, dev_output, subtask_input, dev_input, (int)n).wait();

	queue.enqueueReadBuffer(dev_input, CL_TRUE, 0, sizeof(float) * output.size(), &output.front());
	return output;
}


int main()
{
	std::ios::sync_with_stdio(false);
	try {
		std::vector<cl::Platform> platforms;
		std::vector<cl::Device> devices;
		std::vector<cl::Kernel> kernels;

		cl::Platform::get(&platforms);
		platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &devices);

		cl::Context context(devices);

		cl::CommandQueue queue(context, devices[0], CL_QUEUE_PROFILING_ENABLE);

		std::ifstream cl_file("scan.cl");
		std::string cl_string(std::istreambuf_iterator<char>(cl_file), (std::istreambuf_iterator<char>()));
		cl::Program::Sources source(1, std::make_pair(cl_string.c_str(),
			cl_string.length() + 1));

		cl::Program program(context, source);
		program.build(devices);
		cl::make_kernel<cl::Buffer&, cl::Buffer&, int> block_sums(
			cl::Kernel(program, "block_sums"));
		cl::make_kernel<cl::Buffer&, cl::Buffer&, cl::Buffer&, int> block_add(
			cl::Kernel(program, "block_add"));

		std::cout << "Reading data..." << std::endl;
		std::ifstream input("input.txt");
		int n;
		input >> n;
		std::vector<float> input_a(n);
		for (float &array_elem : input_a)
		{
			input >> array_elem;
		}
		input.close();

		std::cout << "Doing scan..." << std::endl;

		cl::Buffer dev_input(context, CL_MEM_READ_WRITE, sizeof(float) * n);
		cl::Buffer dev_output(context, CL_MEM_READ_WRITE, sizeof(float) * n);

		queue.enqueueWriteBuffer(
			dev_input, CL_TRUE, 0, sizeof(float) * input_a.size(), &input_a.front());

		queue.finish();


		std::vector<float> output = scan(n, dev_input, dev_output, block_sums, block_add, queue, context);

		std::cout << "Writing data..." << std::endl;
		std::ofstream output_file("output.txt");
		for (size_t j = 0; j < output.size(); ++j)
		{
			if (j != 0)
			{
				output_file << ' ';
			}
			output_file << output[j];
		}
		output_file.close();
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