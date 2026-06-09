add_executable(HtmlArticleExtractorTest
  HtmlArticleExtractorTest.cpp
  ${REPO_ROOT}/lib/HtmlArticleExtractor/HtmlArticleExtractor.cpp
)

target_include_directories(HtmlArticleExtractorTest PRIVATE
  ${REPO_ROOT}/lib/HtmlArticleExtractor
)

target_link_libraries(HtmlArticleExtractorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HtmlArticleExtractorTest)
