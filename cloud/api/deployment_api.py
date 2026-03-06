from typing import Dict
from pathlib import Path


def create_deployment_routes(deployment_manager):
    def report_device_status(data: Dict) -> Dict:
        if not deployment_manager:
            return {'status': 'error', 'message': 'Deployment manager not available'}

        device_id = data.get('device_id')
        status = data.get('status')
        if not device_id or not status:
            return {'status': 'error', 'message': 'Missing device_id or status'}

        deployment_manager.report_device_status(
            device_id=device_id,
            status=status,
            current_version=data.get('current_version'),
            error=data.get('error_message'),
        )
        return {'status': 'ok'}

    return {
        'report_device_status': report_device_status,
    }


def register_flask_routes(app, deployment_manager):
    from flask import request, jsonify, send_file

    handlers = create_deployment_routes(deployment_manager)

    @app.route('/api/deployments/device-status', methods=['POST'])
    def report_device_status():
        return jsonify(handlers['report_device_status'](request.json or {}))

    @app.route('/api/deployments/<deployment_id>', methods=['GET'])
    def deployment_status(deployment_id):
        status = deployment_manager.get_deployment_status(deployment_id) if deployment_manager else None
        if not status:
            return jsonify({'status': 'error', 'message': 'Deployment not found'}), 404
        return jsonify({'status': 'ok', 'deployment': status})

    @app.route('/api/models/<version>/download', methods=['GET'])
    def download_model(version):
        if not deployment_manager:
            return jsonify({'status': 'error', 'message': 'Deployment manager not available'}), 500

        model = deployment_manager.get_model_info(version)
        if not model:
            return jsonify({'status': 'error', 'message': 'Model not found'}), 404

        suffix = model.get('metadata', {}).get('artifact_suffix')
        if not suffix:
            suffix = Path(model['file_path']).suffix or '.bin'

        return send_file(
            str(Path(model['file_path']).resolve()),
            as_attachment=True,
            download_name=f'model_{version}{suffix}',
            mimetype='application/octet-stream',
        )
