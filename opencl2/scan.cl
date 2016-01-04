__kernel void block_sums(__global float *input, __global float *output, int n)
{
	__local float local_out[256];
    int gid = get_global_id(0);
    int lid = get_local_id(0);
	int size = get_local_size(0);
	if (gid >= n)
	{
		local_out[lid] = 0;
	} else {
		local_out[lid] = input[gid];
	}
	barrier(CLK_LOCAL_MEM_FENCE);

	int d = 2;
	
	while (d <= size)
	{
		if ((lid + 1) % d == 0)
		{
			local_out[lid] += local_out[lid - (d >> 1)];
		}
		d <<= 1;
		barrier(CLK_LOCAL_MEM_FENCE);
	}
	
	d >>= 1;
	if (lid == size - 1)
	{
		local_out[lid] = 0;
	}
	barrier(CLK_LOCAL_MEM_FENCE);
	
	while (d > 1)
	{
		if ((lid + 1) % d == 0)
		{
			float tmp = local_out[lid];
			local_out[lid] += local_out[lid - (d >> 1)];
			local_out[lid - (d >> 1)] = tmp;
		}
		d >>= 1;
		barrier(CLK_LOCAL_MEM_FENCE);
	}
	
	if (gid >= n)
	{
		return;
	}
	if (gid == n - 1 || lid == size - 1)
	{
		output[gid] = local_out[lid] + input[gid];
	} else {
		output[gid] = local_out[lid + 1];
	}
}

__kernel void block_add(__global float *input, __global float *additions, __global float *output, int n)
{
    int gid = get_global_id(0);
	if (gid >= n)
	{
		return;
	}
	int block_id = get_group_id(0);
	if (block_id > 0)
	{
		output[gid] = input[gid] + additions[block_id - 1];
	} else {
		output[gid] = input[gid];
	}
}