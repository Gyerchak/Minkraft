// One-off tool: writes the default texture pack (textures/*.png) from the
// built-in procedural art so the game ships with editable PNG textures.
#include "../src/Texture.h"
#include "../src/PngLoader.h"
#include <cstdio>
#include <cstdlib>
#include <string>

int main() {
    std::string dir = "textures";
    std::string mkdir = "mkdir -p " + dir;
    if (std::system(mkdir.c_str()) != 0) {
        fprintf(stderr, "failed to create %s\n", dir.c_str());
        return 1;
    }

    for (int t = 0; t < TextureAtlas::NUM_TILES; t++) {
        unsigned char buf[TextureAtlas::TILE * TextureAtlas::TILE * 4];
        TextureAtlas::paintTileContent(t, buf);
        std::string path = dir + "/" + TextureAtlas::tileName(t) + ".png";
        if (!savePng(path.c_str(), buf, TextureAtlas::TILE, TextureAtlas::TILE)) {
            fprintf(stderr, "failed to write %s\n", path.c_str());
            return 1;
        }
        fprintf(stderr, "wrote %s\n", path.c_str());
    }
    return 0;
}
