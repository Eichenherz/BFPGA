#pragma once

#ifndef __HT_CPP_H__
#define __HT_CPP_H__

#include "ht_macros.h"

// NOTE: https://www.foonathan.net/2020/09/move-forward/
#define MOV( ... )  static_cast<decltype( __VA_ARGS__ )&&>( __VA_ARGS__ )
#define FWD( ... )  static_cast<decltype( __VA_ARGS__ )&&>( __VA_ARGS__ )

template<typename T, typename U>
HT_FORCEINLINE T Exchange( T& obj, U&& newVal )
{
	T old = MOV( obj );
	obj = FWD( newVal );
	return old;
}

#endif // !__HT_CPP_H__
