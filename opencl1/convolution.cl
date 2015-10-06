__kernel void convolution(__global float *input_a, __global float *input_b, __global float *output, int n, int m)
{
    int gid_x = get_global_id(1);
    int gid_y = get_global_id(0);
	int half_m = m / 2;

	if (gid_x >= n || gid_y >= n)
	{
		return;
	}
	output[gid_y * n + gid_x] = 0;
	for (int dy = 0; dy < m; ++dy)
	{
		int y = gid_y + dy - half_m;
		if (y < 0 || y >= n) 
		{
			continue;
		}
		for (int dx = 0; dx < m; ++dx)
		{
			int x = gid_x + dx - half_m;
			if (x < 0 || x >= n) 
			{
				continue;
			}
			output[gid_y * n + gid_x] += input_a[y * n + x] * input_b[dy * m + dx];
		}
	}
}