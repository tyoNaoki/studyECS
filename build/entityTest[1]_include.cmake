if(EXISTS "D:/DxLib_VC/DX_LibData/tyoNaoki/entityProject/build/entityTest[1]_tests.cmake")
  include("D:/DxLib_VC/DX_LibData/tyoNaoki/entityProject/build/entityTest[1]_tests.cmake")
else()
  add_test(entityTest_NOT_BUILT entityTest_NOT_BUILT)
endif()
