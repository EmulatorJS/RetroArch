#!/bin/bash
set +e

. ../version.all

if [[ -z "$EMSCRIPTEN" ]] ; then
  echo "Run this script with emmake. Ex: emmake $0"
  exit 1
fi

for i in "$@"; do
  case $i in
    --threads)
      PTHREADS=YES
      shift
      ;;
    --legacy)
      LEGACY=YES
      shift
      ;;
    --clean)
      CLEAN=YES
      shift
      ;;
    --light-debug)
      LIGHT_DEBUG=YES
      shift
      ;;
    *)
      echo "Unknown option $i"
      echo "Usage: $0 [option] ..."
      echo "Options:"
      echo "  --threads"
      echo "  --legacy"
      echo "  --clean"
      echo "  --light-debug"
      exit 1
      ;;
  esac
done

clean () {
  make -C ../ -f Makefile.emulatorjs clean || exit 1
}
containsElement () {
  local e match="$1"
  shift
  for e; do
    if [[ "$e" == "$match" ]]; then
      echo 1
      return 0
    fi
  done
  echo 0
  return 1
}

if [[ "$CLEAN" = "YES" ]]; then
  clean
fi

lastGles=0

largeStack=("mupen64plus_next" "azahar")
useOpenAL=()
largeHeap=("mupen64plus_next" "picodrive" "pcsx_rearmed" "genesis_plus_gx" "genesis_plus_gx_wide" "mednafen_psx" "mednafen_psx_hw" "parallel_n64" "ppsspp" "azahar")
needsGles3=("ppsspp" "azahar")
needsThreads=("ppsspp" "azahar")
largeThreads=("ppsspp" "azahar")
noCHD=("mame2003" "mame2003_plus" "pcsx_rearmed" "genesis_plus_gx" "genesis_plus_gx_wide" "azahar")
no7Zip=("bsnes")
# Cores that perform synchronous file I/O during retro_load_game()
# (e.g. reading XML machine configs) need ASYNCIFY to avoid deadlocking
# the Emscripten event loop.
needsAsync=("mupen64plus_next" "dosbox_pure" "ppsspp" "azahar" "bsnes" "bluemsx")

for f in $(ls -v *_emscripten.bc); do
  name=`echo "$f" | sed "s/\(_libretro_emscripten\|\).bc$//"`
  async=0
  sevenZip=1
  wasm=1
  gles3=1
  stack_mem=4194304 # 4mb
  heap_mem=134217728 # 128mb
  pthread=0
  chd=1
  threads=0
  have_al=0
  have_rwebaudio=1

  if [ "$LEGACY" = "YES" ]; then
    gles3=0
  fi

  if [[ "$PTHREADS" = "YES" ]]; then
    threads=1
    pthread=4
  fi

  if [[ $(containsElement $name "${largeStack[@]}") = 1 ]]; then
    stack_mem=134217728 # 128mb
  fi
  if [[ $(containsElement $name "${largeHeap[@]}") = 1 ]]; then
    heap_mem=536870912 # 512mb
  fi
  if [[ $(containsElement $name "${useOpenAL[@]}") = 1 ]]; then
    have_al=1
    have_rwebaudio=0
  fi
  if [[ $(containsElement $name "${needsThreads[@]}") = 1 && $pthread = 0 ]]; then
    echo "$name"' requires threads! Please build with --threads! Exiting...'
    exit 1
  fi
  if [[ $(containsElement $name "${noCHD[@]}") = 1 ]]; then
    chd=0
  fi
  if [[ $(containsElement $name "${largeThreads[@]}") = 1 ]]; then
    pthread=32
  fi
  if [[ $(containsElement $name "${no7Zip[@]}") = 1 ]]; then
    sevenZip=0
  fi
  if [[ $(containsElement $name "${needsAsync[@]}") = 1 ]]; then
    async=1
  fi
  if [[ $(containsElement $name "${needsGles3[@]}") = 1 && $gles3 = 0 ]]; then
    echo "$name"' does not support gles2 (legacy)! Please build without --legacy! Exiting...'
    exit 1
  fi

  echo "-- Building core: $name --"
  cp -f "$f" ../libretro_emscripten.a
   
  echo NAME: $name
  echo ASYNC: $async
  echo HAVE_THREADS: $threads
  echo PTHREAD_POOL_SIZE: $pthread
  echo GLES3: $gles3
  echo STACK_SIZE: $stack_mem
  echo INITIAL_HEAP: $heap_mem
  echo HAVE_CHD: $chd
  echo HAVE_7ZIP: $sevenZip

  if [[ "$CLEAN" = "YES" ]]; then
    if [ $lastGles != $gles3 ] ; then
        clean
    fi
  fi
  lastGles=$gles3

  light_debug=0
  if [[ "$LIGHT_DEBUG" = "YES" ]]; then
    light_debug=1
  fi

  # Compile core
  echo "BUILD COMMAND: make -C ../ -f Makefile.emulatorjs HAVE_7ZIP=$sevenZip HAVE_CHD=$chd HAVE_THREADS=$threads PTHREAD_POOL_SIZE=$pthread ASYNC=$async HAVE_OPENGLES3=$gles3 STACK_SIZE=$stack_mem INITIAL_HEAP=$heap_mem HAVE_AL=$have_al HAVE_RWEBAUDIO=$have_rwebaudio LIGHT_DEBUG=$light_debug TARGET=${name}_libretro.js -j"$(nproc)
  make -C ../ -f Makefile.emulatorjs HAVE_7ZIP=$sevenZip HAVE_CHD=$chd HAVE_THREADS=$threads PTHREAD_POOL_SIZE=$pthread ASYNC=$async HAVE_OPENGLES3=$gles3 STACK_SIZE=$stack_mem INITIAL_HEAP=$heap_mem HAVE_AL=$have_al HAVE_RWEBAUDIO=$have_rwebaudio LIGHT_DEBUG=$light_debug TARGET=${name}_libretro.js -j$(nproc) || exit 1

  # Move executable files
  out_dir="../../EmulatorJS/data/cores"
  out_name=""

  mkdir -p $out_dir

  core=""
  if [ $name = "mednafen_vb" ]; then
    core="beetle_vb"
  else
    core=${name}
  fi

  out_name=${core}

  if [[ $pthread != 0 ]] ; then
    out_name="${out_name}-thread"
  fi
  if [[ $gles3 = 0 ]] ; then
    out_name="${out_name}-legacy"
  fi
  out_name="${out_name}-wasm.data"

  if [ $wasm = 0 ]; then
    7z a ${out_dir}/${out_name} ../${name}_libretro.js.mem ../${name}_*.js
    rm ../${name}_libretro.js.mem
  else
    7z a ${out_dir}/${out_name} ../${name}_libretro.wasm ../${name}_*.js
    rm ../${name}_libretro.wasm
  fi
  rm -f ../${name}_libretro.js
done
