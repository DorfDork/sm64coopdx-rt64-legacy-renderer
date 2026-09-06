//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Mesh;
	class Scene;
	class Shader;
	class Texture;

	class Instance {
	public:
		struct UniformBlock {
			unsigned int shaderRegister;
			unsigned int size;
			unsigned int dataOffset;
		};
	private:
		Scene *scene;
		Mesh *mesh;
		Texture *diffuseTexture;
		Texture* normalTexture;
		Texture* specularTexture;
		Texture* diffuse2Texture;
		Texture* bumpTexture;
		XMMATRIX transform;
		XMMATRIX previousTransform;
		XMMATRIX normalMatrix;
		float lastTransformRaw[4][4];
		bool lastTransformRawValid;
		RT64_MATERIAL material;
		Shader *shader;
		RT64_RECT scissorRect;
		RT64_RECT viewportRect;
		unsigned int flags;
		std::vector<unsigned char> uniformBlockData;
		std::vector<UniformBlock> uniformBlocks;
	public:
		Instance(Scene *scene);
		virtual ~Instance();
		void setMesh(Mesh *mesh);
		Mesh *getMesh() const;
		void setMaterial(const RT64_MATERIAL &material);
		const RT64_MATERIAL &getMaterial() const;
		void setShader(Shader *shader);
		Shader *getShader() const;
		void setDiffuseTexture(Texture *texture);
		Texture *getDiffuseTexture() const;
		void setNormalTexture(Texture* texture);
		Texture* getNormalTexture() const;
		void setSpecularTexture(Texture* texture);
		Texture* getSpecularTexture() const;
		void setDiffuse2Texture(Texture* texture);
		Texture* getDiffuse2Texture() const;
		void setBumpTexture(Texture* texture);
		Texture* getBumpTexture() const;
		void setTransform(const float m[4][4]);
		XMMATRIX getTransform() const;
		XMMATRIX getNormalMatrix() const;
		void setPreviousTransform(const float m[4][4]);
		XMMATRIX getPreviousTransform() const;
		void setScissorRect(const RT64_RECT &rect);
		RT64_RECT getScissorRect() const;
		bool hasScissorRect() const;
		void setViewportRect(const RT64_RECT &rect);
		RT64_RECT getViewportRect() const;
		bool hasViewportRect() const;
		void setFlags(int v);
		unsigned int getFlags() const;
		void setUniformBlocks(const RT64_SHADER_UNIFORM_BLOCK *blocks, unsigned int blockCount);
		const std::vector<UniformBlock> &getUniformBlocks() const;
		const unsigned char *getUniformBlockData() const;
	};
};