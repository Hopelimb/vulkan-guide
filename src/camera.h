
#include <vk_types.h>
#include <SDL_events.h>

class Camera {
public:

	glm::vec3 velocity;
	glm::vec3 position;

	float pitch{ 0 };
	float yaw{ 0 };

	glm::mat4 getViewMatrix();
	glm::mat4 getRotationMatrix();

	bool camera_rotate_mode{ false };

	void processSDLEvent(SDL_Event& e);

	void update();
};
