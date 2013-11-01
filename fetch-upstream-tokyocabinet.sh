# downloads latest tarball, unpacks it, then commits
# changes to ../tokyocabinet dir

for i in {48..48}; do

#VERSION="1.4.${i}"
VERSION="0.3.4"

VER_MERGED=$(git tag | grep -c "$VERSION")

if [ ! "$VER_MERGED" = "0" ]; then
  echo "Version $VERSION appears to have already been merged"
  exit 1
fi

# fetch new tarball
wget http://fallabs.com/tokyocabinet/pastpkg/tokyocabinet-${VERSION}.tar.gz
#wget http://fallabs.com/tokyocabinet/tokyocabinet-${VERSION}.tar.gz

# unpackage
if [ ! -f tokyocabinet-${VERSION}.tar.gz ]; then
  echo "File tokyocabinet-${VERSION}.tar.gz missing (download failed?)"
  exit 1
fi

tar zxvf tokyocabinet-${VERSION}.tar.gz
rm -f tokyocabinet-${VERSION}.tar.gz
#rm -Rf tokyocabinet-${VERSION}/man
rm -Rf tokyocabinet-${VERSION}/doc/*
touch tokyocabinet-${VERSION}/doc/IntentionallyEmpty.txt

rm -Rf upstream/*
mv tokyocabinet-${VERSION}/* upstream/
rm -Rf tokyocabinet-${VERSION}

# tell git to add any new files as well as include tracked files that were deleted
git add -u
git add -A *
git commit -m "Merged from upstream v${VERSION}"
git tag upstream-version-${VERSION}
#git push -u --tags origin master

done
