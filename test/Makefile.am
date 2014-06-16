# Enable parallel tests
TESTS = T_parseConfig T_stringTools T_smparser T_smp_encode_decode.sh functest.sh

# include the tests on the distribution
EXTRA_DIST = .
# cleanup tests results before including
dist-hook:
	rm -f $(distdir)/.git* $(distdir)/*~ $(distdir)/*.out

clean-local:
	rm -f *.out
