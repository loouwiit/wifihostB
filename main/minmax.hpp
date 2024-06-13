#pragma once

template<class T>
static constexpr T min(T a, T b)
{
	if (a <= b) return a;
	else return b;
}

template<class T>
static constexpr T max(T a, T b)
{
	if (a >= b) return a;
	else return b;
}
