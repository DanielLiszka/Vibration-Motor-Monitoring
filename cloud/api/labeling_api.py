from typing import Dict, Any
import json

def create_labeling_routes(labeling_service):

    def get_next_task(labeler_id: str=None) -> Dict:
        task = labeling_service.get_next_task(labeler_id)
        if task:
            return {'status': 'ok', 'task': task}
        return {'status': 'ok', 'task': None, 'message': 'No tasks available'}

    def get_tasks_batch(labeler_id: str=None, batch_size: int=10) -> Dict:
        tasks = labeling_service.get_tasks_batch(labeler_id, batch_size)
        return {'status': 'ok', 'tasks': tasks, 'count': len(tasks)}

    def submit_label(data: Dict) -> Dict:
        task_id = data.get('task_id')
        label = data.get('label')
        labeler_id = data.get('labeler_id')
        confidence = data.get('confidence', 1.0)
        notes = data.get('notes', '')
        if task_id is None or label is None:
            return {'status': 'error', 'message': 'Missing task_id or label'}
        success = labeling_service.submit_label(task_id, label, labeler_id, confidence, notes)
        if success:
            return {'status': 'ok', 'message': 'Label submitted'}
        return {'status': 'error', 'message': 'Failed to submit label'}

    def skip_task(data: Dict) -> Dict:
        task_id = data.get('task_id')
        reason = data.get('reason', '')
        if task_id is None:
            return {'status': 'error', 'message': 'Missing task_id'}
        success = labeling_service.skip_task(task_id, reason)
        if success:
            return {'status': 'ok', 'message': 'Task skipped'}
        return {'status': 'error', 'message': 'Failed to skip task'}

    def get_task(task_id: int) -> Dict:
        task = labeling_service.get_task(task_id)
        if task:
            return {'status': 'ok', 'task': task}
        return {'status': 'error', 'message': 'Task not found'}

    def get_stats() -> Dict:
        stats = labeling_service.get_stats()
        return {'status': 'ok', 'stats': stats}

    def get_labeler_stats(labeler_id: str=None) -> Dict:
        stats = labeling_service.get_labeler_stats(labeler_id)
        return {'status': 'ok', 'stats': stats}

    def create_batch(data: Dict) -> Dict:
        num_samples = data.get('num_samples', 50)
        strategy = data.get('strategy', 'uncertainty')
        device_id = data.get('device_id')
        task_ids = labeling_service.create_labeling_batch(num_samples, strategy, device_id)
        return {'status': 'ok', 'task_ids': task_ids, 'count': len(task_ids)}

    def get_label_distribution() -> Dict:
        distribution = labeling_service.get_label_distribution()
        return {'status': 'ok', 'distribution': distribution}

    def export_labels(format: str='json') -> str:
        return labeling_service.export_labeled_data(format)
    return {'get_next_task': get_next_task, 'get_tasks_batch': get_tasks_batch, 'submit_label': submit_label, 'skip_task': skip_task, 'get_task': get_task, 'get_stats': get_stats, 'get_labeler_stats': get_labeler_stats, 'create_batch': create_batch, 'get_label_distribution': get_label_distribution, 'export_labels': export_labels}

def register_flask_routes(app, labeling_service):
    from flask import request, jsonify
    handlers = create_labeling_routes(labeling_service)

    @app.route('/api/labeling/next', methods=['GET'])
    def next_task():
        labeler_id = request.args.get('labeler_id')
        return jsonify(handlers['get_next_task'](labeler_id))

    @app.route('/api/labeling/batch', methods=['GET'])
    def batch_tasks():
        labeler_id = request.args.get('labeler_id')
        batch_size = request.args.get('batch_size', 10, type=int)
        return jsonify(handlers['get_tasks_batch'](labeler_id, batch_size))

    @app.route('/api/labeling/submit', methods=['POST'])
    def submit():
        return jsonify(handlers['submit_label'](request.json))

    @app.route('/api/labeling/skip', methods=['POST'])
    def skip():
        return jsonify(handlers['skip_task'](request.json))

    @app.route('/api/labeling/task/<int:task_id>', methods=['GET'])
    def task(task_id):
        return jsonify(handlers['get_task'](task_id))

    @app.route('/api/labeling/stats', methods=['GET'])
    def stats():
        return jsonify(handlers['get_stats']())

    @app.route('/api/labeling/create-batch', methods=['POST'])
    def create_batch():
        return jsonify(handlers['create_batch'](request.json))

    @app.route('/api/labeling/distribution', methods=['GET'])
    def distribution():
        return jsonify(handlers['get_label_distribution']())