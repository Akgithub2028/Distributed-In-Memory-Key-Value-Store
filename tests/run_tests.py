import unittest
import sys
import os

if __name__ == "__main__":
    tests_dir = os.path.join(os.path.dirname(__file__), "integration")
    
    loader = unittest.TestLoader()
    suite = loader.discover(start_dir=tests_dir, pattern="test_*.py")
    
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    
    if result.wasSuccessful():
        print("\nALL INTEGRATION TESTS PASSED SUCCESSFULLY")
        sys.exit(0)
    else:
        print("\nINTEGRATION TESTS FAILED")
        sys.exit(1)
