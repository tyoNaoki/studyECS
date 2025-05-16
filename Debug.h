#pragma once
#include <iostream>

// Can replace these defines with custom macros elsewhere
#ifndef ASSERT
    #define ASSERT(condition, msg) \
		if (!(condition)) { \
			std::cerr << "[ASSERT_ERROR]: " << msg << std::endl; \
			::abort(); \
		}
    #endif
#ifndef CUSTOM_INFO
#ifdef CUSTOM_INFO_ENABLED
    #define CUSTOM_INFO(msg) std::cout << "[INFO]: " << msg << "\n";
#else
    #define CUSTOM_INFO(msg);
#endif
#endif
#ifndef MESSAGE
    #define MESSAGE(msg) std::cout << "[MSG]: " << msg << "\n";
#endif
