document.getElementById('validate-button').addEventListener('click', validateToken);
document.getElementById('toggle-stream-button').addEventListener('click', toggleStream);
document.getElementById('toggle-flashlight-button').addEventListener('click', toggleFlashlight);

addEventListener("beforeunload", (event) => {
    const resultElement = document.getElementById("validation-result");
    if (resultElement.classList.contains("invalid")) {
        return;
    }
    navigator.sendBeacon("/disconnect");
});

function toggleStream() {
    const resultElement = document.getElementById("validation-result");
    if (resultElement.classList.contains("invalid")) {
        alert("Please enter a valid token first!");
        return;
    }

    const img = document.getElementById('stream');
    img.style.display = img.style.display === 'none' ? 'block' : 'none';
    img.src = img.style.display === 'none' ? '' : '/stream';
    fetch('/toggle_stream');
}

async function toggleFlashlight() {
    const resultElement = document.getElementById("validation-result");
    if (resultElement.classList.contains("invalid")) {
        alert("Please enter a valid token first!");
        return;
    }

    const img = document.getElementById('stream');
    img.src = '';
    await fetch('/toggle_flashlight');
    img.src = img.style.display === 'none' ? '' : '/stream';
}

async function validateToken() {
    const token = document.getElementById("token-input").value.trim();
    const resultElement = document.getElementById("validation-result");
    const validateButton = document.getElementById("validate-button");
    const tokenInput = document.getElementById("token-input");

    resultElement.textContent = '...';

    if (!token) {
        resultElement.textContent = "Please enter a token!";
        resultElement.classList.add("invalid");
        return;
    }

    try {
        validateButton.disabled = true;
        const response = await fetch("/validate_token", {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
            },
            body: JSON.stringify({ "token": token }),
        });

        if (response.ok) {
            const { valid } = await response.json();
            if (valid) {
                resultElement.textContent = "\u2714";
                resultElement.classList.add("valid");
                resultElement.classList.remove("invalid");
                tokenInput.value = '';
                tokenInput.placeholder = "Validated!";
                tokenInput.disabled = true;
            } else {
                resultElement.textContent = "\u2718";
                resultElement.classList.add("invalid");
                resultElement.classList.remove("valid");
                validateButton.disabled = false;
            }
        } else {
            throw new Error("Failed to validate token");
        }
    } catch (error) {
        resultElement.textContent = "✘";
        resultElement.classList.add("invalid");
        resultElement.classList.remove("valid");
        console.error("Validation error:", error);
    }
}

