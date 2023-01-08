echo "Collecting…"
if [ ! -d "$INSTALL_PREFIX" ]; then
	echo "ERROR: INSTALL_PREFIX is not set"
	echo "You must set INSTALL_PREFIX to the directory that contains the Git Exam working copy. (Omit the trailing slash character.)"
	exit 1
fi
rm "$INSTALL_PREFIX"/Auto-Grading\ Configuration/custom_validation_code/*
if [ ! -d "$INSTALL_PREFIX"/Build/Linux/x86_64 ]; then
	echo "ERROR: Couldn’t find executables for Linux on x86_64"
	exit 1
fi
if [ ! -d "$INSTALL_PREFIX"/Keys ]; then
	echo "ERROR: Couldn’t find deploy keys"
	exit 1
fi
cp "$INSTALL_PREFIX"/Build/Linux/x86_64/* "$INSTALL_PREFIX"/Auto-Grading\ Configuration/custom_validation_code/
cp "$INSTALL_PREFIX"/Keys/* "$INSTALL_PREFIX"/Auto-Grading\ Configuration/custom_validation_code/

echo "Done!"