rsync -a  --include "*/" \
    --include="*.c" \
    --include="*.cc" \
    --include="*.cpp" \
    --include="*.h" \
    --include="*.hh" \
    --include="*.hpp" \
    --include="CMakeLists.txt" \
    --include="*.sh" \
    --include="meson.build" \
    --exclude="*" tyunyayev@10.0.0.1:Workspace/dpa-xpu ~/ \
    $@


